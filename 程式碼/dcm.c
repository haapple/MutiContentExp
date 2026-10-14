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

int32_t get_i32(const unsigned char* p) {
    return (int32_t)get_u32(p);
}

int vr_uses_long_length(const char* vr) {
    return strcmp(vr, "OB") == 0 || strcmp(vr, "OD") == 0 || strcmp(vr, "OF") == 0 ||
        strcmp(vr, "OL") == 0 || strcmp(vr, "OV") == 0 || strcmp(vr, "OW") == 0 ||
        strcmp(vr, "SQ") == 0 || strcmp(vr, "UC") == 0 || strcmp(vr, "UR") == 0 ||
        strcmp(vr, "UT") == 0 || strcmp(vr, "UN") == 0;
}

/* 常見 Tag 對照字典 */
const char* tag_name(uint16_t group, uint16_t element) {
    if (group == 0x0002 && element == 0x0000) return "FileMetaInformationGroupLength";
    if (group == 0x0002 && element == 0x0001) return "FileMetaInformationVersion";
    if (group == 0x0002 && element == 0x0002) return "MediaStorageSOPClassUID";
    if (group == 0x0002 && element == 0x0003) return "MediaStorageSOPInstanceUID";
    if (group == 0x0002 && element == 0x0010) return "TransferSyntaxUID";
    if (group == 0x0002 && element == 0x0012) return "ImplementationClassUID";
    if (group == 0x0002 && element == 0x0013) return "ImplementationVersionName";

    if (group == 0x0008 && element == 0x0005) return "SpecificCharacterSet";
    if (group == 0x0008 && element == 0x0008) return "ImageType";
    if (group == 0x0008 && element == 0x0012) return "InstanceCreationDate";
    if (group == 0x0008 && element == 0x0013) return "InstanceCreationTime";
    if (group == 0x0008 && element == 0x0016) return "SOPClassUID";
    if (group == 0x0008 && element == 0x0018) return "SOPInstanceUID";
    if (group == 0x0008 && element == 0x0020) return "StudyDate";
    if (group == 0x0008 && element == 0x0021) return "SeriesDate";
    if (group == 0x0008 && element == 0x0022) return "AcquisitionDate";
    if (group == 0x0008 && element == 0x0023) return "ContentDate";
    if (group == 0x0008 && element == 0x0030) return "StudyTime";
    if (group == 0x0008 && element == 0x0031) return "SeriesTime";
    if (group == 0x0008 && element == 0x0032) return "AcquisitionTime";
    if (group == 0x0008 && element == 0x0033) return "ContentTime";
    if (group == 0x0008 && element == 0x0050) return "AccessionNumber";
    if (group == 0x0008 && element == 0x0060) return "Modality";
    if (group == 0x0008 && element == 0x0070) return "Manufacturer";
    if (group == 0x0008 && element == 0x0080) return "InstitutionName";
    if (group == 0x0008 && element == 0x0090) return "ReferringPhysicianName";
    if (group == 0x0008 && element == 0x1010) return "StationName";
    if (group == 0x0008 && element == 0x1030) return "StudyDescription";
    if (group == 0x0008 && element == 0x103E) return "SeriesDescription";
    if (group == 0x0008 && element == 0x1090) return "ManufacturerModelName";
    if (group == 0x0008 && element == 0x1110) return "ReferencedStudySequence";

    if (group == 0x0010 && element == 0x0010) return "PatientName";
    if (group == 0x0010 && element == 0x0020) return "PatientID";
    if (group == 0x0010 && element == 0x0030) return "PatientBirthDate";
    if (group == 0x0010 && element == 0x0040) return "PatientSex";
    if (group == 0x0010 && element == 0x1010) return "PatientAge";

    if (group == 0x0018 && element == 0x0022) return "ScanOptions";
    if (group == 0x0018 && element == 0x0050) return "SliceThickness";
    if (group == 0x0018 && element == 0x0060) return "KVP";
    if (group == 0x0018 && element == 0x1150) return "ExposureTime";
    if (group == 0x0018 && element == 0x1151) return "XRayTubeCurrent";

    if (group == 0x0020 && element == 0x000D) return "StudyInstanceUID";
    if (group == 0x0020 && element == 0x000E) return "SeriesInstanceUID";
    if (group == 0x0020 && element == 0x0011) return "SeriesNumber";
    if (group == 0x0020 && element == 0x0013) return "InstanceNumber";
    if (group == 0x0020 && element == 0x0032) return "ImagePositionPatient";
    if (group == 0x0020 && element == 0x0037) return "ImageOrientationPatient";
    if (group == 0x0020 && element == 0x1041) return "SliceLocation";

    if (group == 0x0028 && element == 0x0002) return "SamplesPerPixel";
    if (group == 0x0028 && element == 0x0004) return "PhotometricInterpretation";
    if (group == 0x0028 && element == 0x0010) return "Rows";
    if (group == 0x0028 && element == 0x0011) return "Columns";
    if (group == 0x0028 && element == 0x0030) return "PixelSpacing";
    if (group == 0x0028 && element == 0x0100) return "BitsAllocated";
    if (group == 0x0028 && element == 0x0101) return "BitsStored";
    if (group == 0x0028 && element == 0x0102) return "HighBit";
    if (group == 0x0028 && element == 0x0103) return "PixelRepresentation";
    if (group == 0x0028 && element == 0x1050) return "WindowCenter";
    if (group == 0x0028 && element == 0x1051) return "WindowWidth";
    if (group == 0x0028 && element == 0x1052) return "RescaleIntercept";
    if (group == 0x0028 && element == 0x1053) return "RescaleSlope";

    if (group == 0x7FE0 && element == 0x0010) return "PixelData";

    // 奇數 Group 皆為製造商私有標籤
    if (group % 2 == 1) return "(PrivateTag_GE)";

    return "(Unknown)";
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

/* 將 Value 轉換為乾淨的 CSV 內容 */
void format_value_for_csv(const unsigned char* data, uint32_t length, const char* vr, char* out, size_t out_size) {
    if (length == 0) {
        strcpy(out, "\"\"");
        return;
    }
    if (length == UNDEFINED_LENGTH) {
        strcpy(out, "\"Undefined Length\"");
        return;
    }

    if (strcmp(vr, "US") == 0 && length >= 2) { snprintf(out, out_size, "%u", get_u16(data)); return; }
    if (strcmp(vr, "UL") == 0 && length >= 4) { snprintf(out, out_size, "%u", get_u32(data)); return; }
    if (strcmp(vr, "SS") == 0 && length >= 2) { snprintf(out, out_size, "%d", get_i16(data)); return; }
    if (strcmp(vr, "SL") == 0 && length >= 4) { snprintf(out, out_size, "%d", get_i32(data)); return; }
    if (strcmp(vr, "FL") == 0 && length >= 4) { float f; memcpy(&f, data, 4); snprintf(out, out_size, "%.4f", f); return; }
    if (strcmp(vr, "FD") == 0 && length >= 8) { double d; memcpy(&d, data, 8); snprintf(out, out_size, "%.4f", d); return; }

    // 文字型 VR
    uint32_t show = (length < 120) ? length : 120;
    char text[256];
    uint32_t idx = 0;
    for (uint32_t i = 0; i < show; i++) {
        unsigned char c = data[i];
        if (c == '\0') break;
        if (isprint(c) && c != '"' && c != ',') {
            text[idx++] = c;
        }
        else if (c == '"') {
            text[idx++] = '\'';
        }
        else {
            text[idx++] = ' ';
        }
    }
    text[idx] = '\0';

    if (idx > 0) {
        snprintf(out, out_size, "\"%s\"", text);
    }
    else {
        snprintf(out, out_size, "<%u bytes binary>", length);
    }
}

/* 記憶體 Buffer 遞迴解析函式 */
long parse_buffer_dataset(const unsigned char* buf, long offset, long end_pos, int depth,
    int stop_on_item_delim, int stop_on_seq_delim, FILE* csv_fp, long* tag_count) {
    while (offset + 4 <= end_pos) {
        long cur_offset = offset;
        uint16_t group = get_u16(&buf[offset]);
        uint16_t element = get_u16(&buf[offset + 2]);
        offset += 4;

        /* 特殊 Tag: FFFE */
        if (group == 0xFFFE) {
            if (offset + 4 > end_pos) break;
            uint32_t len = get_u32(&buf[offset]);
            offset += 4;

            if (element == 0xE000) { // Item Start
                fprintf(csv_fp, "%ld,\"(FFFE,E000)\",--,%u,Item,\"Length=%u\"\n", cur_offset, len, len);
                (*tag_count)++;
                if (len == UNDEFINED_LENGTH) {
                    offset = parse_buffer_dataset(buf, offset, end_pos, depth + 1, 1, 0, csv_fp, tag_count);
                }
                else {
                    long item_end = offset + len;
                    parse_buffer_dataset(buf, offset, item_end, depth + 1, 0, 0, csv_fp, tag_count);
                    offset = item_end;
                }
                continue;
            }
            if (element == 0xE00D) { // Item Delimitation
                fprintf(csv_fp, "%ld,\"(FFFE,E00D)\",--,0,ItemDelimitation,\"\"\n", cur_offset);
                (*tag_count)++;
                if (stop_on_item_delim) return offset;
                continue;
            }
            if (element == 0xE0DD) { // Sequence Delimitation
                fprintf(csv_fp, "%ld,\"(FFFE,E0DD)\",--,0,SequenceDelimitation,\"\"\n", cur_offset);
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

        const char* name = tag_name(group, element);

        /* 擷取全域影像窗格參數 */
        if (group == 0x0028) {
            if (element == 0x0010 && length >= 2) g_rows = get_u16(&buf[offset]);
            if (element == 0x0011 && length >= 2) g_cols = get_u16(&buf[offset]);
            if (element == 0x1050) { g_window_center = atof((const char*)&buf[offset]); g_has_window = 1; }
            if (element == 0x1051) { g_window_width = atof((const char*)&buf[offset]); g_has_window = 1; }
            if (element == 0x1052) g_rescale_intercept = atof((const char*)&buf[offset]);
            if (element == 0x1053) g_rescale_slope = atof((const char*)&buf[offset]);
        }

        /* Sequence 遞迴 */
        if (strcmp(vr, "SQ") == 0) {
            fprintf(csv_fp, "%ld,\"(%04X,%04X)\",%s,%u,%s,\"Sequence (Nested)\"\n",
                cur_offset, group, element, vr, length, name);
            (*tag_count)++;
            if (length == UNDEFINED_LENGTH) {
                offset = parse_buffer_dataset(buf, offset, end_pos, depth + 1, 0, 1, csv_fp, tag_count);
            }
            else {
                long sq_end = offset + length;
                parse_buffer_dataset(buf, offset, sq_end, depth + 1, 0, 0, csv_fp, tag_count);
                offset = sq_end;
            }
            continue;
        }

        /* PixelData 處理 */
        if (group == 0x7FE0 && element == 0x0010) {
            fprintf(csv_fp, "%ld,\"(7FE0,0010)\",%s,%u,PixelData,\"Image Payload (%u bytes)\"\n",
                cur_offset, vr, length, length);
            (*tag_count)++;

            // 主切片 512x512 轉圖
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
                    save_as_bmp("ct_slice.bmp", gray, g_cols, g_rows);
                    free(gray);
                }
            }
            offset += length;
            continue;
        }

        /* 一般欄位輸出至 CSV */
        char val_str[256];
        format_value_for_csv(&buf[offset], length, vr, val_str, sizeof(val_str));
        fprintf(csv_fp, "%ld,\"(%04X,%04X)\",%s,%u,%s,%s\n",
            cur_offset, group, element, vr, length, name, val_str);
        (*tag_count)++;

        offset += length;
    }
    return offset;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

// 預設使用專案內相對路徑
    const char* input_filename = (argc > 1) ? argv[1] :
        "lungDCM/326.4/0a3aee4a-db7c-412f-b5b2-2604f3711399.dcm";

    const char* output_csv = "dicom_tags.csv";

    printf("========================================\n");
    printf("DICOM Buffer-Based Parser & CSV Exporter\n");
    printf("來源檔案: %s\n", input_filename);
    printf("========================================\n\n");

    /* 1. 取得檔案大小並一次性讀進 buffer */
    FILE* fp = fopen(input_filename, "rb");
    if (!fp) {
        // 若「帶資料夾的相對路徑」打不開，備退嘗試同目錄檔案
        input_filename = "0a3aee4a-db7c-412f-b5b2-2604f3711399.dcm";
        fp = fopen(input_filename, "rb");
    }

    if (!fp) {
        printf("無法開啟 DICOM 檔案: %s\n", input_filename);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* buffer = (unsigned char*)malloc(file_size);
    if (!buffer) {
        printf("記憶體配置失敗！\n");
        fclose(fp);
        return 1;
    }

    fread(buffer, 1, file_size, fp);
    fclose(fp); // 讀完立刻關閉檔案

    /* 2. 檢查 DICM Prefix (Offset 128) */
    if (file_size < 132 || memcmp(&buffer[128], "DICM", 4) != 0) {
        printf("錯誤：不是合法的 DICOM 檔案 (未找到 DICM 標頭)！\n");
        free(buffer);
        return 1;
    }

    /* 3. 建立 CSV 檔案 (寫入 UTF-8 BOM) */
    FILE* csv_fp = fopen(output_csv, "wb");
    if (!csv_fp) {
        printf("無法建立 CSV 輸出檔: %s\n", output_csv);
        free(buffer);
        return 1;
    }
    fputc(0xEF, csv_fp); fputc(0xBB, csv_fp); fputc(0xBF, csv_fp);
    fprintf(csv_fp, "Offset,Tag,VR,Length,Name,Value\n");

    /* 4. 解析全緩衝區 Dataset */
    long tag_count = 0;
    parse_buffer_dataset(buffer, 132, file_size, 0, 0, 0, csv_fp, &tag_count);

    fclose(csv_fp);
    free(buffer);

    printf("解析成功！\n");
    printf("檔案大小     : %ld bytes\n", file_size);
    printf("解析 Tag 筆數: %ld 筆\n", tag_count);
    printf("已儲存 CSV   : %s (可用 Excel 直接點開查看)\n", output_csv);
    printf("已儲存圖片   : ct_slice.bmp (512x512 肺窗橫斷面)\n");
    printf("========================================\n");

    return 0;
}