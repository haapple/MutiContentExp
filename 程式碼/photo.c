#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>

#define MAX_PATH_BUFFER 2048
#define MAX_TEXT 128

// 改為相對路徑：從程式執行的資料夾尋找 Photo 目錄
static const wchar_t *PHOTO_DIR = L"Photo";
static const wchar_t *REPORT_PATH = L"Photo/photo_report.csv";

typedef struct {
    char format[16];
    unsigned int width, height;
    int dimensions_ok;
    char status[64];

    char make[MAX_TEXT];
    char model[MAX_TEXT];
    char date_original[MAX_TEXT];
    char date_digitized[MAX_TEXT];
    char date_time[MAX_TEXT];
    char gps_latitude[40];
    char gps_longitude[40];

    int exif_found;
    int exif_seen;
} ImageInfo;

typedef struct {
    int found;
    uint16_t type;
    uint32_t count;
    size_t data_pos;
} TiffEntry;

/* ---------- 基本數值讀取 ---------- */

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24)
        | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8)
        | p[3];
}

static int read_u16(const unsigned char *p, size_t n,
                    size_t off, int little, uint16_t *v)
{
    if (off > n || n - off < 2)
        return 0;

    *v = little ? le16(p + off) : be16(p + off);
    return 1;
}

static int read_u32(const unsigned char *p, size_t n,
                    size_t off, int little, uint32_t *v)
{
    if (off > n || n - off < 4)
        return 0;

    *v = little ? le32(p + off) : be32(p + off);
    return 1;
}

/* ---------- 讀取整個圖片檔案 ---------- */

static unsigned char *load_file(const wchar_t *path, size_t *size)
{
    FILE *fp = _wfopen(path, L"rb");
    unsigned char *buffer;
    __int64 file_size;

    *size = 0;

    if (!fp)
        return NULL;

    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_size = _ftelli64(fp);

    if (file_size < 0 ||
        (uint64_t)file_size > (uint64_t)SIZE_MAX) {
        fclose(fp);
        return NULL;
    }

    if (_fseeki64(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buffer = (unsigned char *)malloc(
        file_size > 0 ? (size_t)file_size : 1
    );

    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)file_size, fp) !=
        (size_t)file_size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    *size = (size_t)file_size;

    return buffer;
}

/* ---------- TIFF / EXIF 資料解析 ---------- */

static size_t tiff_type_size(uint16_t type)
{
    switch (type) {
    case 1: case 2: case 6: case 7:
        return 1;
    case 3: case 8:
        return 2;
    case 4: case 9: case 11: case 13:
        return 4;
    case 5: case 10: case 12:
        return 8;
    default:
        return 0;
    }
}

static TiffEntry find_entry(
    const unsigned char *tiff, size_t n,
    uint32_t ifd_offset, uint16_t wanted_tag, int little)
{
    TiffEntry result = { 0 };
    uint16_t count;
    size_t pos;
    uint32_t i;

    if (!read_u16(tiff, n, ifd_offset, little, &count))
        return result;

    pos = (size_t)ifd_offset + 2;

    if (pos > n || count > (n - pos) / 12)
        return result;

    for (i = 0; i < count; i++, pos += 12) {
        uint16_t tag, type;
        uint32_t items, offset;
        size_t unit;
        uint64_t total;
        size_t data_pos;

        if (!read_u16(tiff, n, pos, little, &tag))
            return result;

        if (tag != wanted_tag)
            continue;

        if (!read_u16(tiff, n, pos + 2, little, &type) ||
            !read_u32(tiff, n, pos + 4, little, &items) ||
            !read_u32(tiff, n, pos + 8, little, &offset))
            return result;

        unit = tiff_type_size(type);

        if (unit == 0)
            return result;

        total = (uint64_t)unit * items;

        if (total <= 4)
            data_pos = pos + 8;
        else
            data_pos = offset;

        if (data_pos > n || total > n - data_pos)
            return result;

        result.found = 1;
        result.type = type;
        result.count = items;
        result.data_pos = data_pos;

        return result;
    }

    return result;
}

static void copy_ascii(
    const unsigned char *tiff, size_t n,
    TiffEntry e, char *out, size_t capacity)
{
    size_t i, count;

    if (capacity == 0)
        return;

    out[0] = '\0';

    if (!e.found || e.type != 2 || e.count == 0)
        return;

    count = e.count;

    if (count >= capacity)
        count = capacity - 1;

    if (e.data_pos > n || count > n - e.data_pos)
        return;

    for (i = 0; i < count; i++) {
        unsigned char c = tiff[e.data_pos + i];

        if (c == '\0')
            break;

        out[i] = (c >= 32 && c <= 126) ? (char)c : ' ';
    }

    while (i > 0 && out[i - 1] == ' ')
        i--;

    out[i] = '\0';
}

static int read_long_entry(
    const unsigned char *tiff, size_t n,
    TiffEntry e, int little, uint32_t *value)
{
    if (!e.found || e.count < 1 ||
        (e.type != 4 && e.type != 13))
        return 0;

    return read_u32(tiff, n, e.data_pos, little, value);
}

static int read_rational(
    const unsigned char *tiff, size_t n,
    size_t pos, int little, double *value)
{
    uint32_t a, b;

    if (!read_u32(tiff, n, pos, little, &a) ||
        !read_u32(tiff, n, pos + 4, little, &b) ||
        b == 0)
        return 0;

    *value = (double)a / b;
    return 1;
}

static void read_gps(
    const unsigned char *tiff, size_t n,
    TiffEntry coordinate, TiffEntry reference,
    int little, int latitude, char *out, size_t capacity)
{
    double degrees, minutes, seconds, decimal;
    char ref = '\0';
    size_t pos;

    out[0] = '\0';

    if (!coordinate.found ||
        coordinate.type != 5 ||
        coordinate.count < 3)
        return;

    pos = coordinate.data_pos;

    if (!read_rational(tiff, n, pos, little, &degrees) ||
        !read_rational(tiff, n, pos + 8, little, &minutes) ||
        !read_rational(tiff, n, pos + 16, little, &seconds))
        return;

    if (reference.found &&
        reference.type == 2 &&
        reference.count > 0 &&
        reference.data_pos < n) {
        ref = (char)tiff[reference.data_pos];
    }

    if (latitude) {
        if (ref != 'N' && ref != 'S')
            return;
    } else {
        if (ref != 'E' && ref != 'W')
            return;
    }

    decimal = degrees + minutes / 60.0 + seconds / 3600.0;

    if ((latitude && ref == 'S') ||
        (!latitude && ref == 'W'))
        decimal = -decimal;

    snprintf(out, capacity, "%.8f", decimal);
}

static void parse_exif(
    const unsigned char *payload, size_t payload_size,
    ImageInfo *info)
{
    const unsigned char *tiff;
    size_t n;
    int little;
    uint16_t magic;
    uint32_t first_ifd, exif_ifd = 0, gps_ifd = 0;
    TiffEntry e;

    if (payload_size < 14 ||
        memcmp(payload, "Exif\0\0", 6) != 0)
        return;

    info->exif_seen = 1;

    tiff = payload + 6;
    n = payload_size - 6;

    if (tiff[0] == 'I' && tiff[1] == 'I')
        little = 1;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
        little = 0;
    else
        return;

    if (!read_u16(tiff, n, 2, little, &magic) ||
        magic != 42 ||
        !read_u32(tiff, n, 4, little, &first_ifd))
        return;

    info->exif_found = 1;

    e = find_entry(tiff, n, first_ifd, 0x010F, little);
    copy_ascii(tiff, n, e, info->make, sizeof(info->make));

    e = find_entry(tiff, n, first_ifd, 0x0110, little);
    copy_ascii(tiff, n, e, info->model, sizeof(info->model));

    e = find_entry(tiff, n, first_ifd, 0x0132, little);
    copy_ascii(tiff, n, e, info->date_time, sizeof(info->date_time));

    e = find_entry(tiff, n, first_ifd, 0x8769, little);

    if (read_long_entry(tiff, n, e, little, &exif_ifd)) {
        e = find_entry(tiff, n, exif_ifd, 0x9003, little);
        copy_ascii(tiff, n, e, info->date_original,
                   sizeof(info->date_original));

        e = find_entry(tiff, n, exif_ifd, 0x9004, little);
        copy_ascii(tiff, n, e, info->date_digitized,
                   sizeof(info->date_digitized));
    }

    e = find_entry(tiff, n, first_ifd, 0x8825, little);

    if (read_long_entry(tiff, n, e, little, &gps_ifd)) {
        TiffEntry lat_ref, lat, lon_ref, lon;

        lat_ref = find_entry(tiff, n, gps_ifd, 0x0001, little);
        lat = find_entry(tiff, n, gps_ifd, 0x0002, little);
        lon_ref = find_entry(tiff, n, gps_ifd, 0x0003, little);
        lon = find_entry(tiff, n, gps_ifd, 0x0004, little);

        read_gps(tiff, n, lat, lat_ref, little, 1,
                 info->gps_latitude, sizeof(info->gps_latitude));

        read_gps(tiff, n, lon, lon_ref, little, 0,
                 info->gps_longitude, sizeof(info->gps_longitude));
    }
}

/* ---------- JPEG 格式與尺寸 ---------- */

static int is_sof(int marker)
{
    return marker == 0xC0 || marker == 0xC1 ||
           marker == 0xC2 || marker == 0xC3 ||
           marker == 0xC5 || marker == 0xC6 ||
           marker == 0xC7 || marker == 0xC9 ||
           marker == 0xCA || marker == 0xCB ||
           marker == 0xCD || marker == 0xCE ||
           marker == 0xCF;
}

static void parse_jpeg(
    const unsigned char *data, size_t n, ImageInfo *info)
{
    size_t pos = 2;

    if (n < 4 || data[0] != 0xFF || data[1] != 0xD8)
        return;

    strcpy_s(info->format, sizeof(info->format), "JPEG");

    while (pos < n) {
        int marker;
        uint16_t segment_length;
        size_t payload_size;
        const unsigned char *payload;

        while (pos < n && data[pos] != 0xFF)
            pos++;

        while (pos < n && data[pos] == 0xFF)
            pos++;

        if (pos >= n)
            break;

        marker = data[pos++];

        if (marker == 0xD9 || marker == 0xDA)
            break;

        if (marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD7))
            continue;

        if (pos + 2 > n)
            break;

        segment_length = be16(data + pos);
        pos += 2;

        if (segment_length < 2)
            break;

        payload_size = segment_length - 2;

        if (payload_size > n - pos)
            break;

        payload = data + pos;

        if (marker == 0xE1) {
            parse_exif(payload, payload_size, info);
        }

        if (is_sof(marker) && payload_size >= 5) {
            info->height = be16(payload + 1);
            info->width = be16(payload + 3);

            if (info->width > 0 && info->height > 0)
                info->dimensions_ok = 1;
        }

        pos += payload_size;
    }
}

/* ---------- PNG / BMP / GIF 尺寸 ---------- */

static void analyse_image(
    const unsigned char *data, size_t n, ImageInfo *info)
{
    memset(info, 0, sizeof(*info));
    strcpy_s(info->format, sizeof(info->format), "Unknown");
    strcpy_s(info->status, sizeof(info->status), "Unsupported format");

    if (n >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        parse_jpeg(data, n, info);

        if (info->dimensions_ok)
            strcpy_s(info->status, sizeof(info->status), "OK");
        else
            strcpy_s(info->status, sizeof(info->status),
                     "Cannot read dimensions");

        return;
    }

    if (n >= 24 &&
        memcmp(data, "\x89PNG\r\n\x1A\n", 8) == 0 &&
        memcmp(data + 12, "IHDR", 4) == 0) {
        strcpy_s(info->format, sizeof(info->format), "PNG");
        info->width = be32(data + 16);
        info->height = be32(data + 20);
    }
    else if (n >= 26 && data[0] == 'B' && data[1] == 'M') {
        uint32_t dib_size = le32(data + 14);

        strcpy_s(info->format, sizeof(info->format), "BMP");

        if (dib_size >= 40) {
            int32_t w = (int32_t)le32(data + 18);
            int32_t h = (int32_t)le32(data + 22);

            if (w > 0 && h != 0 && h != INT32_MIN) {
                info->width = (unsigned int)w;
                info->height = (unsigned int)(h < 0 ? -h : h);
            }
        }
    }
    else if (n >= 10 &&
             (memcmp(data, "GIF87a", 6) == 0 ||
              memcmp(data, "GIF89a", 6) == 0)) {
        strcpy_s(info->format, sizeof(info->format), "GIF");
        info->width = le16(data + 6);
        info->height = le16(data + 8);
    }

    if (info->width > 0 && info->height > 0) {
        info->dimensions_ok = 1;
        strcpy_s(info->status, sizeof(info->status), "OK");
    }
}

/* ---------- CSV 輸出 ---------- */

static void csv_text(FILE *fp, const char *text)
{
    const char *p = text;

    fputc('"', fp);

    while (*p) {
        if (*p == '"')
            fputc('"', fp);

        fputc(*p++, fp);
    }

    fputc('"', fp);
}

static void csv_wide(FILE *fp, const wchar_t *text)
{
    int needed = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);

    char *utf8;

    if (needed <= 0) {
        csv_text(fp, "");
        return;
    }

    utf8 = (char *)malloc((size_t)needed);

    if (!utf8) {
        csv_text(fp, "");
        return;
    }

    if (WideCharToMultiByte(
            CP_UTF8, 0, text, -1, utf8, needed, NULL, NULL) > 0) {
        csv_text(fp, utf8);
    } else {
        csv_text(fp, "");
    }

    free(utf8);
}

static int is_image_file(const wchar_t *name)
{
    const wchar_t *dot = wcsrchr(name, L'.');

    if (!dot)
        return 0;

    return _wcsicmp(dot, L".jpg") == 0 ||
           _wcsicmp(dot, L".jpeg") == 0 ||
           _wcsicmp(dot, L".png") == 0 ||
           _wcsicmp(dot, L".bmp") == 0 ||
           _wcsicmp(dot, L".gif") == 0;
}

/* ---------- 主程式 ---------- */

int main(void)
{
    wchar_t pattern[MAX_PATH_BUFFER];
    wchar_t full_path[MAX_PATH_BUFFER];
    WIN32_FIND_DATAW fd;
    HANDLE handle;
    FILE *csv;
    int found = 0, dimensions = 0, jpeg_count = 0, exif_count = 0;

    if (swprintf_s(pattern, MAX_PATH_BUFFER, L"%ls\\*", PHOTO_DIR) < 0) {
        printf("ERROR: Photo path is too long.\n");
        return 1;
    }

    handle = FindFirstFileW(pattern, &fd);

    if (handle == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot find Photo folder.\n");
        printf("Check PHOTO_DIR in the code.\n");
        return 1;
    }

    csv = _wfopen(REPORT_PATH, L"wb");

    if (!csv) {
        printf("ERROR: Cannot create photo_report.csv.\n");
        FindClose(handle);
        return 1;
    }

    /* 讓 Excel 能正確讀取 UTF-8 */
    fwrite("\xEF\xBB\xBF", 1, 3, csv);

    fprintf(csv,
        "filename,format,width,height,file_size_bytes,"
        "make,model,date_time_original,date_time_digitized,"
        "date_time,gps_latitude,gps_longitude,exif_status,status\r\n");

    do {
        wchar_t *name = fd.cFileName;
        unsigned char *data;
        size_t size = 0;
        ImageInfo info;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (!is_image_file(name))
            continue;

        if (swprintf_s(full_path, MAX_PATH_BUFFER,
                       L"%ls\\%ls", PHOTO_DIR, name) < 0)
            continue;

        data = load_file(full_path, &size);

        if (data) {
            analyse_image(data, size, &info);
            free(data);
        } else {
            memset(&info, 0, sizeof(info));
            strcpy_s(info.format, sizeof(info.format), "Unknown");
            strcpy_s(info.status, sizeof(info.status), "Cannot read file");
        }

        csv_wide(csv, name);
        fputc(',', csv);

        csv_text(csv, info.format);
        fputc(',', csv);

        if (info.dimensions_ok)
            fprintf(csv, "%u,%u,%llu,",
                    info.width, info.height,
                    (unsigned long long)size);
        else
            fprintf(csv, ",,%llu,", (unsigned long long)size);

        csv_text(csv, info.make);
        fputc(',', csv);
        csv_text(csv, info.model);
        fputc(',', csv);
        csv_text(csv, info.date_original);
        fputc(',', csv);
        csv_text(csv, info.date_digitized);
        fputc(',', csv);
        csv_text(csv, info.date_time);
        fputc(',', csv);
        csv_text(csv, info.gps_latitude);
        fputc(',', csv);
        csv_text(csv, info.gps_longitude);
        fputc(',', csv);

        if (strcmp(info.format, "JPEG") == 0) {
            jpeg_count++;

            if (info.exif_found) {
                csv_text(csv, "EXIF found");
                exif_count++;
            } else if (info.exif_seen) {
                csv_text(csv, "EXIF unreadable");
            } else {
                csv_text(csv, "No EXIF");
            }
        } else {
            csv_text(csv, "Not JPEG");
        }

        fputc(',', csv);
        csv_text(csv, info.status);
        fprintf(csv, "\r\n");

        found++;

        if (info.dimensions_ok)
            dimensions++;

    } while (FindNextFileW(handle, &fd));

    FindClose(handle);
    fclose(csv);

    printf("Photo scan completed.\n");
    printf("Image files found: %d\n", found);
    printf("Images with dimensions read: %d\n", dimensions);
    printf("JPEG files checked for EXIF: %d\n", jpeg_count);
    printf("JPEG files with readable EXIF: %d\n", exif_count);
    printf("Report saved to:\n");
    printf("%ls\n", REPORT_PATH); // 動態印出相對路徑

    printf("\nEXIF fields may be blank if the photos do not contain them.\n");

    return 0;
}