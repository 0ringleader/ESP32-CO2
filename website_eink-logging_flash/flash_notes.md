# Current Steps

## Folder structure after creating files

```
website_eink-logging_flash/
├── website_eink-logging_flash.ino
├── partitions.csv
└── data/
    ├── index.html
    ├── style.css
    └── script.js
```

## How to upload

1. **Upload the sketch** (Arduino IDE):
   - Select board: `ESP32S3 Dev Module`
   - Select partition scheme: `Custom partition table (in sketch)`
   - Set Flash Size: `16MB`
   - Upload the `.ino` file

2. **Upload the LittleFS data** (web assets):
   - Install the "ESP32 LittleFS Data Upload" plugin for Arduino IDE
   - Tools → ESP32 Sketch Data Upload
   - This uploads contents of the `data/` folder to the LittleFS partition

3. **First boot**: 
   - The device creates `/log.csv` with a header automatically
   - On subsequent boots it loads the last 144 samples into RAM for charts
   - New data is appended to the CSV every 10 minutes

## New features in this version

- **Persistent CSV logging** on LittleFS (`/log.csv`)
- **Download CSV** button on web UI
- **Clear log** button (with confirmation)
- **Separate HTML/CSS/JS** served from LittleFS
- **Auto log rotation** when file exceeds 10 MB
- **Loads history on boot** from CSV into RAM buffer

