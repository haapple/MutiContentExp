---

### 一、 程式現在怎麼抓檔案？

程式現在全部採用**相對路徑**，不管你把專案放到哪台電腦，只要放在專案資料夾下執行，程式就會自動去對應的資料夾抓檔：

```text
 (專案主資料夾)
├── parse_cpap.exe
├── dcm.exe
├── photo.exe
├── cpap/
│   └── 1-CPAP.TXT             <-- [CPAP 程式] 自動去這裡抓檔
├── lungDCM/
│   └── 326.4/
│       └── 0a3aee4a-*.dcm     <-- [DICOM 程式] 自動去這裡抓檔
└── Photo/                     <-- [Photo 程式] 自動抓這裡面的所有照片

```

* **`parse_cpap`**：預設去抓 `cpap/1-CPAP.TXT`（也可以後面帶參數指定別的檔）。
* **`dcm`**：預設去抓 `lungDCM/326.4/0a3aee4a-*.dcm`。
* **`photo`**：自動掃描 `Photo/` 資料夾內的所有 JPG/PNG 照片。

---

### 二、 怎麼啟動？

在專案資料夾下開啟終端機（Terminal / CMD），執行以下指令：

#### 步驟 1：編譯程式 (產生 .exe)

```bash
gcc parse_cpap.c -o parse_cpap
gcc dcm.c -o dcm
gcc photo.c -o photo

```

#### 步驟 2：直接執行

```bash
./parse_cpap    # 執行 CPAP 解析 -> 自動產出 cpap_trend.csv
./dcm           # 執行 DICOM 解析 -> 自動產出 dicom_tags.csv 和 ct_slice.bmp
./photo         # 執行 照片分析 -> 自動產出 Photo/photo_report.csv

```

> **小技巧（指定其他檔案執行）：**
> 如果想讀別的 CPAP 或 DICOM 檔案，直接在後面加路徑即可：
> * `./parse_cpap cpap/2-CPAP.TXT output2.csv`
> * `./parse_cpap lungDCM/326.4/other.dcm`
> 
>