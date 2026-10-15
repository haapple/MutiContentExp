#define _CRT_SECURE_NO_WARNINGS
#pragma execution_character_set("utf-8")

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define UNDEFINED_LENGTH 0xFFFFFFFFu

/* 影像規格全域變數 */
int g_rows = 512;
int g_cols = 512;
double g_rescale_slope = 1.0;
double g_rescale_intercept = -1024.0;
double g_window_center = -550.0;
double g_window_width = 1500.0;
int g_has_window = 0;
int g_total_processed = 0;

/* 從 buffer 讀取 16-bit / 32-bit Little Endian 整數 */
uint16_t get_u16(const unsigned char* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t get_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int16_t get_i16(const unsigned char* p) {
    return (int16_t)get_u16(p);
}

int vr_uses_long_length(const char* vr) {
    return strcmp(vr, "OB") == 0 || strcmp(vr, "OD") == 0 || strcmp(vr, "OF") == 0 ||
        strcmp(vr, "OL") == 0 || strcmp(vr, "OV") == 0 || strcmp(vr, "OW") == 0 ||
        strcmp(vr, "SQ") == 0 || strcmp(vr, "UC") == 0 || strcmp(vr, "UR") == 0 ||
        strcmp(vr, "UT") == 0 || strcmp(vr, "UN") == 0;
}

/* 儲存 24-bit 灰階 BMP 圖檔 */
void save_as_bmp(const char* out_filename, const uint8_t* gray_data, int width, int height) {
    FILE* fp = fopen(out_filename, "wb");
    if (!fp) return;

    int row_padded = (width * 3 + 3) & (~3);
    uint32_t image_size = (uint32_t)row_padded * height;
    uint32_t file_size = 54 + image_size;

    uint8_t file_header[14] = {
        'B', 'M',
        (uint8_t)(file_size), (uint8_t)(file_size >> 8), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
        0, 0, 0, 0, 54, 0, 0, 0
    };

    uint8_t dib_header[40] = {
        40, 0, 0, 0,
        (uint8_t)(width), (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)(height), (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0, 24, 0, 0, 0, 0, 0
    };
    memcpy(&dib_header[20], &image_size, 4);

    fwrite(file_header, 1, 14, fp);
    fwrite(dib_header, 1, 40, fp);

    uint8_t pad[3] = { 0, 0, 0 };
    int pad_len = row_padded - width * 3;

    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            uint8_t gray = gray_data[y * width + x];
            uint8_t bgr[3] = { gray, gray, gray };
            fwrite(bgr, 1, 3, fp);
        }
        if (pad_len > 0) fwrite(pad, 1, pad_len, fp);
    }
    fclose(fp);
}

/* 記憶體 Buffer 遞迴解析 DICOM Dataset (純解析，無 CSV) */
long parse_buffer_dataset(const unsigned char* buf, long offset, long end_pos, int depth,
    int stop_on_item_delim, int stop_on_seq_delim, long* tag_count, const char* bmp_name) {
    while (offset + 4 <= end_pos) {
        uint16_t group = get_u16(&buf[offset]);
        uint16_t element = get_u16(&buf[offset + 2]);
        offset += 4;

        /* 特殊 Tag: FFFE */
        if (group == 0xFFFE) {
            if (offset + 4 > end_pos) break;
            uint32_t len = get_u32(&buf[offset]);
            offset += 4;

            if (element == 0xE000) { // Item Start
                (*tag_count)++;
                if (len == UNDEFINED_LENGTH) {
                    offset = parse_buffer_dataset(buf, offset, end_pos, depth + 1, 1, 0, tag_count, bmp_name);
                }
                else {
                    long item_end = offset + len;
                    parse_buffer_dataset(buf, offset, item_end, depth + 1, 0, 0, tag_count, bmp_name);
                    offset = item_end;
                }
                continue;
            }
            if (element == 0xE00D) { // Item Delimitation
                (*tag_count)++;
                if (stop_on_item_delim) return offset;
                continue;
            }
            if (element == 0xE0DD) { // Sequence Delimitation
                (*tag_count)++;
                if (stop_on_seq_delim) return offset;
                continue;
            }
            break;
        }

        /* 一般 Data Element: 讀取 VR 與 Length */
        if (offset + 2 > end_pos) break;
        char vr[3] = { (char)buf[offset], (char)buf[offset + 1], '\0' };
        offset += 2;

        uint32_t length = 0;
        if (vr_uses_long_length(vr)) {
            offset += 2; // 跳過 2 bytes 保留位
            if (offset + 4 > end_pos) break;
            length = get_u32(&buf[offset]);
            offset += 4;
        }
        else {
            if (offset + 2 > end_pos) break;
            length = get_u16(&buf[offset]);
            offset += 2;
        }

        /* 擷取全域影像窗格與尺寸參數 */
        if (group == 0x0028) {
            if (element == 0x0010 && length >= 2) g_rows = get_u16(&buf[offset]);
            if (element == 0x0011 && length >= 2) g_cols = get_u16(&buf[offset]);

            if ((element == 0x1050 || element == 0x1051 || element == 0x1052 || element == 0x1053) && length > 0) {
                char tmp_val[64] = { 0 };
                size_t copy_len = (length < sizeof(tmp_val) - 1) ? length : (sizeof(tmp_val) - 1);
                memcpy(tmp_val, &buf[offset], copy_len);
                tmp_val[copy_len] = '\0';

                if (element == 0x1050) { g_window_center = atof(tmp_val); g_has_window = 1; }
                if (element == 0x1051) { g_window_width = atof(tmp_val); g_has_window = 1; }
                if (element == 0x1052) g_rescale_intercept = atof(tmp_val);
                if (element == 0x1053) g_rescale_slope = atof(tmp_val);
            }
        }

        /* Sequence 遞迴 */
        if (strcmp(vr, "SQ") == 0) {
            (*tag_count)++;
            if (length == UNDEFINED_LENGTH) {
                offset = parse_buffer_dataset(buf, offset, end_pos, depth + 1, 0, 1, tag_count, bmp_name);
            }
            else {
                long sq_end = offset + length;
                parse_buffer_dataset(buf, offset, sq_end, depth + 1, 0, 0, tag_count, bmp_name);
                offset = sq_end;
            }
            continue;
        }

        /* PixelData 處理：轉換 HU 肺窗並儲存 BMP */
        if (group == 0x7FE0 && element == 0x0010) {
            (*tag_count)++;

            if (length >= (uint32_t)(g_rows * g_cols * 2)) {
                int count = g_rows * g_cols;
                uint8_t* gray = (uint8_t*)malloc(count);
                if (gray) {
                    double wc = g_has_window ? g_window_center : -600.0;
                    double ww = g_has_window ? g_window_width : 1500.0;
                    if (wc > 0 && wc < 1000) wc = -wc; // 肺窗調整
                    double min_hu = wc - (ww / 2.0);
                    double max_hu = wc + (ww / 2.0);

                    for (int i = 0; i < count; i++) {
                        int16_t raw_val = get_i16(&buf[offset + i * 2]);
                        double hu = (double)raw_val * g_rescale_slope + g_rescale_intercept;
                        if (hu <= min_hu) gray[i] = 0;
                        else if (hu >= max_hu) gray[i] = 255;
                        else gray[i] = (uint8_t)(((hu - min_hu) / ww) * 255.0);
                    }
                    save_as_bmp(bmp_name, gray, g_cols, g_rows);
                    free(gray);
                }
            }
            offset += length;
            continue;
        }

        (*tag_count)++;
        offset += length;
    }
    return offset;
}

/* 解析單一 DICOM 檔案 */
void process_dicom_file(const char* filepath) {
    // 1. 初始化切片全域變數
    g_rows = 512;
    g_cols = 512;
    g_rescale_slope = 1.0;
    g_rescale_intercept = -1024.0;
    g_window_center = -550.0;
    g_window_width = 1500.0;
    g_has_window = 0;

    // 2. 開啟檔案
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 132) {
        fclose(fp);
        return;
    }

    unsigned char* buffer = (unsigned char*)malloc(file_size);
    if (!buffer) {
        fclose(fp);
        return;
    }

    fread(buffer, 1, file_size, fp);
    fclose(fp);

    // 3. 檢查 DICM 檔頭標記 (Offset 128)
    if (memcmp(&buffer[128], "DICM", 4) != 0) {
        free(buffer);
        return; // 非標準 DICOM 檔則略過
    }

    // 4. 生成對應的 BMP 檔名（例如 0a3aee4a...dcm -> 0a3aee4a...bmp）
    const char* filename_only = strrchr(filepath, '/');
    if (!filename_only) filename_only = strrchr(filepath, '\\');
    filename_only = filename_only ? filename_only + 1 : filepath;

    char bmp_name[512];
    const char* dot = strrchr(filename_only, '.');
    int name_len = dot ? (int)(dot - filename_only) : (int)strlen(filename_only);
    snprintf(bmp_name, sizeof(bmp_name), "%.*s.bmp", name_len, filename_only);

    // 5. 開始解析 Dataset
    long tag_count = 0;
    parse_buffer_dataset(buffer, 132, file_size, 0, 0, 0, &tag_count, bmp_name);

    free(buffer);
    g_total_processed++;
    printf("[成功解析] %s -> 產生 BMP: %s (%dx%d, %ld Tags)\n", filepath, bmp_name, g_cols, g_rows, tag_count);
}

/* 遞迴掃描目錄下所有 DICOM 檔案 */
void scan_and_process(const char* path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        char search_pattern[1024];
        snprintf(search_pattern, sizeof(search_pattern), "%s\\*", path);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search_pattern, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                    continue;

                char subpath[1024];
                snprintf(subpath, sizeof(subpath), "%s\\%s", path, fd.cFileName);
                scan_and_process(subpath); // 遞迴進子目錄
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
    else {
        process_dicom_file(path);
    }
#else
    process_dicom_file(path);
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    // 如果命令行有傳入參數就用參數，否則自動嘗試常見的相對路徑位置
    const char* default_targets[] = {
        "lungDCM",
        "../lungDCM",
        "../../lungDCM",
        "../fhir-Hospital-main/lungDCM",
        "../../fhir-Hospital-main/lungDCM"
    };

    printf("========================================\n");
    printf("DICOM 批次自動掃描解析器 (全相對路徑版)\n");
    printf("========================================\n\n");

    if (argc > 1) {
        // 使用者手動傳入相對或絕對路徑
        scan_and_process(argv[1]);
    }
    else {
        // 自動嘗試相對路徑
        int found = 0;
        int num_candidates = sizeof(default_targets) / sizeof(default_targets[0]);

        for (int i = 0; i < num_candidates; i++) {
            DWORD attr = GetFileAttributesA(default_targets[i]);
            if (attr != INVALID_FILE_ATTRIBUTES) {
                printf("自動定位相對路徑成功: %s\n\n", default_targets[i]);
                scan_and_process(default_targets[i]);
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("警告: 找不到 lungDCM 資料夾！請確認 lungDCM 是否與程式碼在同一層或上層目錄。\n");
        }
    }

    printf("\n========================================\n");
    printf("解析完畢！共成功處理 %d 筆 DICOM 檔案並匯出 BMP 圖檔。\n", g_total_processed);
    printf("========================================\n");

    return 0;
}
