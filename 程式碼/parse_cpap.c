#define _CRT_SECURE_NO_WARNINGS
#pragma execution_character_set("utf-8")

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================
   輔助工具：將 buffer 中連續 3 個 ASCII 數字轉為整數
   例如 buffer[idx] ~ buffer[idx+2] 為 '0', '4', '9' -> 49
   ========================================================= */
int parse_3digits(const unsigned char* buf, long idx)
{
    if (!isdigit(buf[idx]) || !isdigit(buf[idx + 1]) || !isdigit(buf[idx + 2])) {
        return -1; // 格式不合規
    }
    return (buf[idx] - '0') * 100 + (buf[idx + 1] - '0') * 10 + (buf[idx + 2] - '0');
}

int main(int argc, char* argv[])
{
    // 設定終端機使用 UTF-8，防止中文顯示亂碼
    SetConsoleOutputCP(65001);

// 若沒有帶命令列參數，預設使用專案內部的「相對路徑」
    const char* input_path = (argc > 1) ? argv[1] : "cpap/1-CPAP.TXT";
    const char* output_path = (argc > 2) ? argv[2] : "cpap_trend.csv";

    printf("========================================\n");
    printf("CPAP Buffer-Based Parser\n");
    printf("來源檔案: %s\n", input_path);
    printf("輸出檔案: %s\n", output_path);
    printf("========================================\n\n");

    /* -----------------------------------------------------
       步驟一：讀取檔案（比照你的範例寫法）
       ----------------------------------------------------- */
    FILE* pFile = fopen(input_path, "rb");
    if (!pFile) {
        printf("錯誤：無法開啟來源檔案！\n");
        return 1;
    }

    // 計算檔案總大小
    fseek(pFile, 0, SEEK_END);
    long fileSize = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);

    // 配置與檔案大小相同的 buffer，並一次性讀入記憶體
    unsigned char* buffer = (unsigned char*)malloc(fileSize);
    if (!buffer) {
        printf("錯誤：記憶體配置失敗！\n");
        fclose(pFile);
        return 1;
    }

    fread(buffer, sizeof(char), fileSize, pFile);
    fclose(pFile); // 讀取完畢即可直接關閉檔案

    printf("檔案讀取完成，總位元組數：%ld bytes\n\n", fileSize);

    /* -----------------------------------------------------
       步驟二：建立並初始化輸出 CSV
       ----------------------------------------------------- */
    FILE* csv = fopen(output_path, "wb");
    if (!csv) {
        printf("錯誤：無法建立 CSV 輸出檔案！\n");
        free(buffer);
        return 1;
    }

    // 寫入 UTF-8 BOM，避免 Windows Excel 開啟亂碼
    fputc(0xEF, csv);
    fputc(0xBB, csv);
    fputc(0xBF, csv);

    fprintf(csv,
        "日期,"
        "時間,"
        "設定壓力(cmH2O),"
        "瞬間壓力(cmH2O),"
        "10分鐘平均壓力(cmH2O),"
        "漏氣量(L/min),"
        "10分鐘呼吸中止次數,"
        "本週累計時數(小時)\n"
    );
    /* -----------------------------------------------------
       步驟三：用 Index 遍歷 Buffer 解析數值
       ----------------------------------------------------- */
    long record_count = 0;

    // 每個完整週期為 3 個封包 (21 * 3 = 63 bytes)
    // 遍歷 buffer，直到剩餘長度不足 63 bytes 為止
    for (long i = 0; i + 62 < fileSize; i++)
    {
        /*
           檢查封包特徵：
           1. 封包 1 開頭為 0x02，型態為 'T' (第 19 碼)，結尾為 0x03 (第 20 碼)
           2. 封包 2 開頭為 0x02，型態為 0xFF，結尾為 0x03
           3. 封包 3 開頭為 0x02，型態為 0xFF，結尾為 0x03
        */
        if (buffer[i] == 0x02 && buffer[i + 19] == 'T' && buffer[i + 20] == 0x03 &&
            buffer[i + 21] == 0x02 && buffer[i + 40] == 0xFF && buffer[i + 41] == 0x03 &&
            buffer[i + 42] == 0x02 && buffer[i + 61] == 0xFF && buffer[i + 62] == 0x03)
        {
            /* ---------- 封包 1（T）：日期時間與設定/瞬間壓 ---------- */
            // buffer[i+1] ~ buffer[i+12] 為 YYYYMMDDHHMM
            char date[11];
            snprintf(date, sizeof(date), "%.4s-%.2s-%.2s", &buffer[i + 1], &buffer[i + 5], &buffer[i + 7]);

            char time[6];
            snprintf(time, sizeof(time), "%.2s:%.2s", &buffer[i + 9], &buffer[i + 11]);

            // buffer[i+13] ~ buffer[i+15] 為目標設定壓力（例如 050 -> 5.0）
            int set_p_raw = parse_3digits(buffer, i + 13);
            double set_p = (set_p_raw >= 0) ? (set_p_raw / 10.0) : 0.0;

            // buffer[i+16] ~ buffer[i+18] 為當下瞬間壓力（例如 049 -> 4.9）
            int cur_p_raw = parse_3digits(buffer, i + 16);
            double cur_p = (cur_p_raw >= 0) ? (cur_p_raw / 10.0) : 0.0;

            /* ---------- 封包 2（FF_A）：本週時數與 10 分鐘平均壓 ---------- */
            // buffer[i+28] ~ buffer[i+30] 為本週累積時數（例如 016 -> 1.6 小時）
            int wh_raw = parse_3digits(buffer, i + 28);
            double weekly_hours = (wh_raw >= 0) ? (wh_raw / 10.0) : 0.0;

            // buffer[i+31] ~ buffer[i+33] 為過去 10 分鐘平均壓力（例如 046 -> 4.6）
            int avg_p_raw = parse_3digits(buffer, i + 31);
            double avg_p = (avg_p_raw >= 0) ? (avg_p_raw / 10.0) : 0.0;

            /* ---------- 封包 3（FF_B）：呼吸中止次數與漏氣量 ---------- */
            // buffer[i+55] ~ buffer[i+57] 為過去 10 分鐘呼吸中止事件數（例如 003 -> 3）
            int apnea_events = parse_3digits(buffer, i + 55);

            // buffer[i+58] ~ buffer[i+60] 為漏氣量（例如 040 -> 40 L/min）
            int leak = parse_3digits(buffer, i + 58);

            // 寫入 CSV
            fprintf(csv, "%s,%s,%.1f,%.1f,%.1f,%d,%d,%.1f\n",
                    date, time, set_p, cur_p, avg_p, leak, apnea_events, weekly_hours);

            record_count++;

            // 跳過這組已經解析完畢的 63 位元組，加速走訪
            i += 62;
        }
    }

    fclose(csv);
    free(buffer); // 釋放記憶體

    printf("========================================\n");
    printf("解析完成！\n");
    printf("共擷取 T 紀錄週期數：%ld 筆\n", record_count);
    printf("CSV 輸出路徑：%s\n", output_path);
    printf("========================================\n");

    return 0;
}