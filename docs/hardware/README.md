# docs/hardware: ESP32-LyraT V4.3 schematic, layout, BOM

Yahan board ka official Espressif design data hai, taaki project banane wale ko kahin aur dhoondhna na pade. Download date: 2026-10-27.

| File | Kya hai | Source (Espressif) |
|---|---|---|
| `espressif-downloads/ESP32-LYRAT_V4.3-20220119-schematic.pdf` | **Official schematic**, Rev 4.3, dated Wednesday, January 19, 2022, 1 sheet, size D. Yahi version ESP-ADF hardware reference page link karta hai. | https://dl.espressif.com/dl/schematics/ESP32-LYRAT_V4.3-20220119.pdf |
| `espressif-downloads/esp32-lyrat-v4.3-schematic-older.pdf` | Purana V4.3 schematic upload (same board, pehle ka export). Reference ke liye; naya wala use karo. | https://dl.espressif.com/dl/schematics/esp32-lyrat-v4.3-schematic.pdf |
| `espressif-downloads/ESP32-LyraT_v4.3_component_layout.pdf` | Component layout: page 1 top view (saare designators, header pin names, 95.5 × 80.6 mm), page 2 bottom view. | https://dl.espressif.com/dl/schematics/ESP32-LyraT_v4.3_component_layout.pdf |
| `schematic-pages/*.png` | Upar wale PDFs ke rendered pages (schematic 200 dpi full + 6 tiles 170 dpi, layout 150 dpi). Bina PDF viewer ke dekhne ke liye. | generated locally (PyMuPDF) |
| `schematic-pages/*.txt` | PDF ka text layer (designators, values, net names). | generated locally |
| `lyrat-v4.3-bom-from-schematic.csv` | Schematic text layer se nikala designator → value/part (290 rows). `?` = value text layer mein designator ke saath nahi mila (pin number ya alag line par). | generated locally |
| `BOM-hinglish.md` | Curated BOM: project ke liye kya kharidna + board ke main ICs/connectors/passives, verified schematic tiles se. | yeh folder |

Online references (download nahi kiye, link kaafi hai):

- Hardware reference page (block diagram, pin table, DIP table): https://docs.espressif.com/projects/esp-adf/en/latest/multimedia-boards/dev-boards/board-esp32-lyrat-v4.3.html
- Getting started (kya chahiye, jumpers, pehla power-on): https://docs.espressif.com/projects/esp-adf/en/latest/multimedia-boards/dev-boards/get-started-esp32-lyrat.html
- ES8388 datasheet (Everest): http://www.everest-semi.com/pdf/ES8388%20DS.pdf (mirror jo kaam kiya: https://www.pawpaw.cn/media/documents/2021-12/ES8388_DS.pdf)
- ESP32-WROVER-E datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf
- ESP32 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- Espressif audio hardware design guidelines: https://www.espressif.com/sites/default/files/documentation/esp32_audio_design_guidelines__en.pdf
- ES8388 reference driver (ESP-ADF): `../references/es8388-reference.c.txt` (license saath mein)

Note: Espressif board ko "Proprietary" mark karta hai; schematic padhne/seekhne/apna firmware banane ke liye publicly available hai. PCB Gerber/Altium files public nahi hain, isliye "apna board print karna" is guide ka hissa nahi; ready LyraT V4.3 board kharidna hai.
