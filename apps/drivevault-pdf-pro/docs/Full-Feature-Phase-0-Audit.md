# Phase 0 — ตรวจสอบ DriveVault PDF Pro 1.3.4

ตรวจซอร์สที่ commit `34b37de` และเทียบ backlog 114 ข้อจาก `DriveVault-PDF-Pro-Full-Feature-Development.md` ตามลำดับ A–P ตัวเลขแต่ละแถวตรงกับลำดับข้อในหมวดนั้น ชื่อไฟล์อ้างจากโฟลเดอร์แอป ยกเว้นเอกสาร suite ระดับราก

ผลจำแนกตามรายการรวม **working 14, partial 60, missing 17, blocked 23, placeholder 0** ฟังก์ชันที่ทำงานจริงในรายการ partial นับรวมอยู่ในรายการนั้น เช่น ส่งออก PNG/JPEG ทำงานได้ แต่รายการแปลงทั้งหมวดยังไม่ครบ

- **working** มีเส้นทางใช้งานจริงตามขอบเขตที่ระบุ พร้อมหลักฐานซอร์สหรือเทสต์ **partial** ทำได้บางส่วน **missing** ไม่พบเส้นทางใช้งาน **blocked** ต้องมีเอนจิน บริการ อุปกรณ์ หรือเงื่อนไขทดสอบเพิ่มเติม **placeholder** หมายถึง UI ที่แสดงความสามารถแต่ไม่มี implementation ไม่พบในคำสั่งที่เปิดใช้งานและตรวจเส้นทางแล้ว
- คอลัมน์เกณฑ์ตรวจใช้ติดตามงานที่เหลือ ผลเทสต์ปัจจุบันดูหัวข้อการตรวจสอบท้ายเอกสาร คำว่า working ไม่ครอบคลุม fixture หรือความสามารถอื่นนอกหลักฐานในแถวนั้น

## สถาปัตยกรรมและจุดเข้าถึง

| ส่วน | ซอร์สและเส้นทางคำสั่ง |
|---|---|
| Shell | `src/ui/Main.qml` ประกอบ Home, tabs, viewer, หมวดเครื่องมือ, file picker, drop, password และ modal ปิดงานค้าง `Ctrl+O` เปิดไฟล์, `Ctrl+K` ค้นคำสั่ง, `F11` เต็มจอ |
| Home และแท็บ | `HomeScreen.qml` เปิด/ค้น/ลบ recent และเปิดโฟลเดอร์ การ์ดเรียก `Main.runTool` `FileTabs.qml` เปิด/สลับ/ปิดแท็บ ปิดผ่าน `ViewerScreen.closeTabAt` ตรวจ dirty state |
| ตัวอ่าน | `ViewerScreen.qml`, `PageView.qml`, `SidePanel.qml` จัดหน้า/ซูม/ค้น/พิมพ์/หมุน/thumbnail/outline/comment และเมนู copy/select-all `Ctrl+0/1/2` พอดีหน้า/100%/พอดีกว้าง, `Ctrl+W` ปิด, `Ctrl+Tab` สลับ, `Ctrl+F/F3` ค้น, `Ctrl+P` พิมพ์ |
| คำสั่งเอกสาร | `Main.runTool` → `ViewerScreen.runTool` → dialog หรือ model รวม/จัด/แยกหน้าใช้ Organizer/Split แก้ข้อความใช้ ReplaceText แยกจาก EditBar สำหรับเนื้อหาที่เพิ่มเอง กรอกแบบฟอร์มใช้ FormDialog คำสั่ง Output เปิด ImageExport/Security/Maintenance/Batch/Redaction |
| แอปและ backend | `src/app/main.cpp` ลงทะเบียน Doc, Organizer, Compressor, Editor, Forms และ image providers `DocumentController` จัดการแท็บ การตั้งค่า การค้นหาและงานเขียน `EditorModel` เก็บ undo/redo และ recovery แบบ QSaveFile |
| PDF/งานเขียน | `src/pdfengine/PdfDocument.cpp` เป็นขอบเขต PDFium ภายใต้ mutex `src/pdftools` ใช้ qpdf เขียนสำเนาและจัดการโครงสร้าง `PrintTask.cpp` ใช้ Qt Print Support งาน render/ค้นหา/ส่งออกมี worker pool ตามงาน การอ่าน PDF ยังอยู่ใน process เดียว |
| Build/installer | `CMakeLists.txt` C++20, Qt Quick/QML, Ninja/MSVC `scripts/build.ps1 -Test` และ `tests/CMakeLists.txt` รัน 12 suites `installer/windows/pdf.iss` และ `scripts/package.ps1` ใช้ Master Pro, manifest, registry, dependency/license gates และ SHA-256 |

## Dependencies และทางเลือก

| ตัวเลือก | ความสามารถและข้อจำกัด |
|---|---|
| PDFium Chromium 8138, BSD-3 | render/selection/forms/annotations และ API เขียนเอกสารบางส่วน ตัวแอปใช้ qpdf สำหรับงานเขียนเชิงโครงสร้าง การรองรับ incremental save ของเอนจินไม่ใช่สัญญาว่าจะรักษาลายเซ็น แอปนี้ยังไม่มีการตรวจ chain/trust ปิด V8/XFA ในแพ็กเกจ |
| qpdf 12.4.2, Apache-2.0 | โครงสร้าง, encryption, rewrite, page copy และ selective replacement/redaction สำหรับกรณีที่โค้ดรองรับ การจัดย่อหน้า ฟอร์มขั้นสูง และ tagged PDF ยังต้องสร้าง logic และ fixture เฉพาะ |
| Qt 6.8.3, LGPL-3.0 แบบ shared DLL | UI, Qt Print Support, QPdfWriter และ I/O ใช้โมดูลที่ผ่าน audit แล้ว แอปแยกการบันทึกสำเนาและ temp/recovery จากต้นฉบับ ใช้ Inter/Noto Sans Thai แบบ OFL |
| ONNX Runtime 1.24.4 MIT และ PaddleOCR Apache-2.0 | CPU local, ตัวตรวจจับ PP-OCRv3 และตัวรู้จำไทย PP-OCRv5 พร้อม alphabet ที่ตรวจแฮชตอนแพ็ก การสแกนฮาร์ดแวร์ต้องมี adapter WIA/TWAIN และทดสอบอุปกรณ์จริง |
| Office desktop automation | ทางเลือกในอนาคตต้องพึ่ง Office ที่ติดตั้งและสิทธิ์ใช้งาน การทำ unattended มีข้อจำกัดการสนับสนุน/เสถียรภาพตาม Microsoft การนำเข้าจาก Office กับการสร้าง Office จาก PDF เป็นคนละปัญหา [Microsoft Office automation](https://support.microsoft.com/en-us/visio/considerations-for-server-side-automation-of-office) |
| LibreOffice headless / commercial SDK | ทางเลือกสำหรับทดลองแปลงเอกสารในอนาคต ประเมิน deployment, isolation, Thai fonts และ layout เทียบเอกสารจริงก่อนเลือก OSS มีภาระการผนวกและดูแลเอง ส่วน commercial ต้องขอราคา/สิทธิ์แจกจ่ายและพิสูจน์ capability ต่อ SKU ยังไม่มีการเลือก SDK หรืออนุมัติค่าใช้จ่าย [LibreOffice document conversion](https://help.libreoffice.org/latest/en-US/text/shared/guide/ms_user.html) |
| Standards / AI / cloud / signatures | ต้องมี validator หรือ provider จริงตามงาน พร้อม capability gate, opt-in, credential store และ integration test ขอบเขตปัจจุบันเป็น offline ตามแผนเดิม |

แยก service เพิ่มตามขอบเขตงานจริง ใช้ API ปัจจุบันเป็นสัญญาระหว่าง QML และ backend งาน conversion, certificate, cloud และ AI ควรมี adapter และ capability status ของตัวเองเมื่อมี implementation งาน isolation ต้องทดสอบ timeout, crash, cleanup และ permission กับไฟล์ไม่น่าเชื่อถือก่อนเปิดใช้ รายการใบอนุญาตที่จัดส่งอยู่ใน `docs/THIRD-PARTY-NOTICES.txt` และตรวจด้วย `scripts/license-audit.ps1`

ใช้ Adobe เป็นข้อมูลเปรียบเทียบหมวดงานและการนำทาง [Acrobat features](https://www.adobe.com/acrobat/features) · [Desktop release notes](https://helpx.adobe.com/acrobat/desktop/whats-new/release-notes.html) · [Page navigation](https://helpx.adobe.com/acrobat/desktop/get-started/learn-the-basics/overview.html)

## ตารางเทียบ backlog

### A

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| A1 | partial | Home/recent/search/local folder/quick actions; pinned/favorites/folder browser missing | `HomeScreen.qml`, `DocumentController.cpp` | ค้นหา เปิด ลบ และ pin หลัง restart |
| A2 | partial | Open single/drag/recent/recovery; picker/drop first file only; association is installer code | `Main.qml`, `EditorModel.cpp`, `pdf.iss`, `main.cpp` | เปิดหลายไฟล์รวมไฟล์ติดรหัสจาก picker/drop/Open With |
| A3 | partial | Tab switch/close/dirty prompt; no reorder/reopen/context menu | `FileTabs.qml`, `ViewerScreen.qml` | เรียง ปิด กู้แท็บ และยืนยัน dirty |
| A4 | partial | Continuous/spread/fullscreen; no cover/facing/single-page paging/reading mode | `ViewerScreen.qml`, `Main.qml` | ตรวจลำดับหน้าทุกโหมด |
| A5 | partial | PDFium renders fonts/images/vectors/transparency; no OCG controls or dedicated OCG fixture | `PdfDocument.cpp`, `PageImageProvider.cpp` | เทียบพิกเซลและสลับ OCG |
| A6 | partial | Zoom presets/100%/fit page+width/scroll/view rotation; arbitrary typed zoom, fit height and hand pan absent | `ViewerScreen.qml` | ทดสอบขนาด/การเลื่อน/rotation โดยไฟล์ไม่เปลี่ยน |
| A7 | partial | Thumbnails/outline/comments; attachments in separate tool; no layers/labels/structure navigation | `SidePanel.qml`, `MaintenanceDialog.qml` | กระโดดไปยังหน้า/label/outline และสลับ layer |
| A8 | partial | Document search with case/whole-word and highlights; no bookmark/comment search | `SearchBar.qml`, `DocumentController.cpp` | ตรวจผลทุกชนิดและการยกเลิก |
| A9 | partial | Text selection/copy/link confirmation; no object/image selection or snapshot | `ViewerScreen.qml`, `PdfDocument.cpp` | คัดลอกไทยตามช่วงและคัดภาพตามพิกเซล |
| A10 | partial | Metadata and attachments tool; no consolidated font/security/PDF-version properties | `MaintenanceDialog.qml`, `DocumentMaintenance.cpp` | ตรวจข้อมูลกับไฟล์ fixture |
| A11 | missing | TTS | `ViewerScreen.qml ไม่มีคำสั่ง` | อ่านไทย/อังกฤษและแจ้ง voice ที่ขาด |
| A12 | partial | Print dialog, shortcuts, TH/EN and light/dark; no preview/density/scale setting | `PrintDialog.qml`, `PreferencesDialog.qml` | แสดง preview และทดสอบ keyboard/scale |

### B

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| B1 | partial | Insert blank page in organizer; standalone blank and full paper settings missing | `OrganizerDialog.qml`, `PageListModel.cpp` | สร้างไฟล์ A4/Letter/Legal/custom พร้อม orientation/margins |
| B2 | missing | Office/images/HTML/text to PDF | `Main.qml`, `PdfTools.cpp ไม่มี importer` | ตรวจไฟล์แต่ละชนิดและ fidelity |
| B3 | missing | Multi-image PDF creation | `ImageExport.cpp ทำ PDF → รูป` | สร้าง/เรียง/หมุน/fit รูปหลายหน้า |
| B4 | missing | Mixed-format combine | `OrganizerDialog.qml รับ PDF` | รวมชนิดต่างกันและเทียบลำดับ |
| B5 | working | Print to a PDF file via Qt print pipeline | `PrintDialog.qml`, `PrintTask.cpp`, `tst_controller` | เปิดผล PDF และตรวจช่วงหน้า |
| B6 | blocked | Web capture/scanner/clipboard creation; web engine excluded; scanner and clipboard not implemented | `PDF-Pro-Plan.md`, `Main.qml` | ทดสอบอุปกรณ์จริงและรูปจาก clipboard |
| B7 | partial | Embedded files management; no PDF Portfolio creation | `MaintenanceDialog.qml`, `DocumentMaintenance.cpp` | อ่านและสร้าง collection แบบมาตรฐาน |
| B8 | blocked | PDF/A and PDF/X export lack external validation | `package.ps1`, `PdfTools.cpp` | ผ่าน validator อิสระก่อนติดป้าย conformance |

### C

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| C1 | partial | Supported horizontal native text replacement; no arbitrary block insertion/deletion/move/reflow | `ReplaceTextDialog.qml`, `ReplaceText.cpp`, `PdfDocument.cpp` | แก้เนื้อหาจริงและตรวจ text extraction |
| C2 | partial | Font/size/style/color/alignment for added text; no full original typography controls | `EditBar.qml`, `Overlay.cpp` | เทียบผลพิกเซลและตัวอักษร |
| C3 | partial | Thai overlay shaping and searchable output; original extraction/embedding fidelity varies | `Overlay.cpp`, `tst_editor`, `tst_pdfengine` | ทดสอบเอกสารไทยจริงและ font substitution |
| C4 | partial | Added image placement/move/scale/rotate/delete; no original image replacement/crop workflow | `EditBar.qml`, `EditorModel.cpp` | แก้ต้นฉบับและตรวจภาพหลังบันทึก |
| C5 | partial | Added shapes/stroke/fill; no native vector or hotspot editing | `EditBar.qml`, `Overlay.cpp` | แก้วัตถุเดิมและลิงก์โดยไม่ทับเนื้อหา |
| C6 | partial | Text/images on selected page and editing of added objects; full guides/snap not established | `EditBar.qml`, `EditorModel.cpp` | จัดตำแหน่งและลำดับชั้นหลังเปิดซ้ำ |
| C7 | partial | Watermark/page numbers; no full hyperlink/header/footer/Bates/background editor | `EditBar.qml`, `MarkDialog.qml`, `Overlay.cpp` | ตรวจเลขลำดับและลิงก์หลายหน้า |
| C8 | partial | Single supported run replacement; no batch/multi-page find-replace | `ReplaceText.cpp` | แก้หลายหน้าและปฏิเสธ overflow |
| C9 | partial | Metadata editable; outline display only; page label editor absent | `MaintenanceDialog.qml`, `SidePanel.qml` | แก้แล้วเปิดตรวจทุกฟิลด์ |
| C10 | working | Native replacement and added content are separate actions; OCR writes searchable layer | `Main.qml`, `ReplaceText.cpp`, `ThaiOcr.cpp` | ทดสอบแยกผลแต่ละ pipeline |
| C11 | partial | Dirty/undo/recovery/save copy; signature handling varies by operation | `EditorModel.cpp`, `ReplaceText.cpp`, `PdfTools.cpp` | ตรวจ dirty/recovery และคำเตือนทุกทางเขียน |

### D

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| D1 | working | Thumbnail grid and drag reorder | `OrganizerDialog.qml`, `PageListModel.cpp`, `tst_organizer` | ตรวจลำดับข้อความผลลัพธ์ |
| D2 | partial | Insert/delete/rotate/crop/duplicate/move; no dedicated replace or cross-document clipboard workflow | `OrganizerDialog.qml`, `PdfTools.cpp` | ทดสอบ replace/copy ข้ามไฟล์ |
| D3 | working | Range/count/size/bookmark split and export selected pages via organizer | `SplitDialog.qml`, `SplitPlan.cpp`, `tst_pdftools` | ตรวจจำนวนหน้า ชื่อไฟล์ และ qpdf |
| D4 | working | Merge with ordered preview | `OrganizerDialog.qml`, `PdfTools.cpp`, `tst_organizer` | ผลตรงกับ preview |
| D5 | partial | Crop box editing; no full box/label/size scaling editor | `PageListModel.cpp`, `PdfTools.cpp` | อ่านผล Media/Crop/Bleed/Trim/Art |
| D6 | working | Multi-selection operations with undo and redo | `PageListModel.cpp`, `OrganizerDialog.qml`, `tst_organizer` | ย้อนและทำซ้ำหลายหน้า |

### E

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| E1 | partial | PNG/JPEG image output; no Office/plain text/RTF/HTML export UI | `ImageExport.cpp`, `ImageExportDialog.qml` | ตรวจไทย ลำดับ ตาราง และภาพทุกฟอร์แมต |
| E2 | missing | Office/image/text to PDF and multi-conversion | `Main.qml`, `PdfTools.cpp` | แปลงชุดหลายชนิด |
| E3 | working | Page range/quality/DPI/output file, folder or ZIP for image export | `ImageExportDialog.qml`, `ImageExport.cpp`, `tst_output` | ตรวจ DPI/ชื่อ/จำนวน/พิกเซล |
| E4 | blocked | Office layout, tables and Thai fidelity need conversion engine and fixtures | `PDF-Pro-Plan.md`, `ImageExport.cpp` | เทียบเอกสารจริงกับโปรแกรมปลายทาง |
| E5 | partial | Searchable PDF OCR separate from export; no integrated OCR-before-export | `OcrDialog.qml`, `ImageExportDialog.qml` | OCR แล้ว export จาก pipeline เดียว |
| E6 | partial | Multi-page export and batch PDF operations; no multi-file format conversion | `ImageExport.cpp`, `BatchPdf.cpp` | วัด progress/cancel/failure ต่อไฟล์ |
| E7 | missing | Complex layout conversion warning; no applicable converter | `ImageExportDialog.qml` | แสดงข้อจำกัดก่อนส่งออก |

### F

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| F1 | blocked | Scanner acquisition requires device integration and hardware | `ThaiOcr.cpp`, `OcrDialog.qml` | ทดสอบ WIA/TWAIN และยกเลิกจริง |
| F2 | partial | Thai recognition/searchable copy; mixed Thai-English coverage not qualified | `ThaiOcr.cpp`, `OcrDialog.qml`, `tst_output` | ตรวจ OCR fixture ไทย/อังกฤษหลายหน้า |
| F3 | missing | OCR proofreading/editable recognition view | `OcrDialog.qml` | แก้ผล OCR และค้นซ้ำ |
| F4 | missing | Scan cleanup and deskew/orientation workflow | `ThaiOcr.cpp` | เทียบผลกับภาพเอียง/มี noise |
| F5 | missing | Scanner auto detection, scan imports and recognized text export | `Main.qml`, `OcrDialog.qml` | scan หลายหน้าและส่งออกข้อความ |
| F6 | partial | Range progress/cancel for one PDF; no directory batch/confidence review | `ThaiOcr.cpp`, `OcrDialog.qml` | ทดสอบชุดไฟล์และ low-confidence report |
| F7 | working | Searchable layer written over retained original page content | `ThaiOcr.cpp` | เทียบ render ก่อน/หลังและค้นข้อความ |

### G

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| G1 | partial | Highlight/underline/strike tools; squiggly and insert/replace review markups absent | `EditBar.qml`, `Overlay.cpp` | ตรวจชนิด annotation หลังเปิดใหม่ |
| G2 | partial | Sticky/text/pen/shapes; callout and eraser workflow incomplete | `EditBar.qml`, `EditorModel.cpp` | ตรวจรายการและ undo/redo |
| G3 | partial | ตราประทับมีข้อความ preset และแก้ข้อความได้ ส่วนตราแบบกำหนดภาพและการตั้งตัวตนยังขาด | `EditBar.qml`, `EditorModel.cpp` | สร้าง custom stamp และตรวจ author |
| G4 | partial | Read comments and notes; no threads/resolve/search filters | `SidePanel.qml`, `PdfDocument.cpp` | กรอง ตอบ และ resolve หลัง reopen |
| G5 | partial | Print rendering can include annotations; no XFDF/FDF or summary exporter | `PrintTask.cpp`, `SidePanel.qml` | round-trip annotations และ report |
| G6 | blocked | Shared review requires backend | `Main.qml ไม่มีการเชื่อมต่อ` | ทดสอบ sync conflict และ consent |

### H

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| H1 | partial | Text/check/radio/combo/list supported; no existing push-button actions; Thai field constraints | `FormDialog.qml`, `Forms.cpp`, `tst_forms` | round-trip ทุกชนิดรวมไทย |
| H2 | missing | Form designer | `FormDialog.qml` | สร้าง ย้าย ตั้งชื่อ และเปิดใหม่ |
| H3 | partial | Read-only/options checks; no rule/calculation/tab-order authoring | `Forms.cpp` | ตรวจ rule/validation/appearance |
| H4 | blocked | No auto-detection; XFA disabled in audited PDFium | `package.ps1`, `FormDialog.qml` | fixture AcroForm และ XFA |
| H5 | missing | Opt-in form flattening workflow | `FormDialog.qml` | ตรวจ fields หายและหน้าตาคงเดิม |
| H6 | partial | Generic text, pen and image insertion can place a visual mark; dedicated signature and reusable initials workflow absent | `EditBar.qml`, `EditorModel.cpp` | ตรวจพิมพ์ วาด นำเข้า และ reuse |
| H7 | blocked | PAdES verification/signing/TSA/trust service absent | `ReplaceText.cpp มีเพียง guard` | ตรวจ signed fixture และ trust/revocation |
| H8 | missing | No signature-specific disclosure in generic text, pen or image tools | `EditBar.qml`, `Tr.qml` | ข้อความชัดทั้งสองภาษา |

### I

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| I1 | working | AES-256 with user/owner passwords | `SecurityDialog.qml`, `Security.cpp`, `tst_output` | เปิดด้วยรหัสถูกและปฏิเสธรหัสผิด |
| I2 | partial | Permission flags written and limitations shown; enforcement across all reader actions unqualified | `Security.cpp`, `DocumentController.cpp` | ตรวจ copy/edit/print ตาม permission |
| I3 | working | Authorized password-based unlock | `SecurityDialog.qml`, `Security.cpp`, `tst_output` | ทดสอบ user/owner/wrong credentials |
| I4 | partial | Selective removal on supported content plus image-only route for complex documents | `Redaction.cpp`, `SelectiveRedaction.cpp`, `tst_redaction` | ตรวจ raw streams, image, OCR, annotations |
| I5 | working | Regex/presets, reviewed marks, verification | `RedactionBar.qml`, `RedactionAudit.cpp`, `tst_redaction` | ค้นซ้ำและตรวจช่องทางข้อมูลรั่ว |
| I6 | partial | Metadata/attachments cleanup and stronger redaction-specific cleanup; general sanitizer incomplete | `DocumentMaintenance.cpp`, `Redaction.cpp` | ทดสอบ scripts/hidden layers และ embedded files |
| I7 | partial | PDFium without V8/XFA; no isolated untrusted worker/sanitizer guarantee | `PdfDocument.cpp`, `package.ps1` | fuzz/isolation และตรวจ sanitizer |
| I8 | partial | Warnings/guards in some writing paths; no certificate verification | `ReplaceText.cpp`, `DocumentMaintenance.cpp` | signed fixture ก่อน/หลังทุกทางเขียน |
| I9 | partial | Local-only and safe outputs; no audit events/privacy policy controls UI | `DocumentController.cpp`, `Security.cpp` | ตรวจ settings/logs/file permissions |

### J

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| J1 | missing | Compare text/pages/visual changes | `CompressDialog.qml มีเพียง preview การบีบอัด` | ตรวจเพิ่ม ลบ และแก้หน้า |
| J2 | missing | Diff list, jump, report | `ViewerScreen.qml` | ตรวจตำแหน่งและรายงาน |
| J3 | partial | Metadata/annotations/basic links available; no comprehensive inspection | `DocumentMaintenance.cpp`, `PdfDocument.cpp` | ตรวจ font/link/PDF-A/signatures |
| J4 | blocked | Preflight needs validated profile engine | `package.ps1` | เทียบรายงานจาก validator อิสระ |

### K

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| K1 | working | Compression presets/downsampling/JPEG and measurements | `CompressDialog.qml`, `Compress.cpp`, `tst_compress` | ยืนยันขนาด/ภาพ/ข้อความ |
| K2 | partial | Structure compression and linearization; no general font subset optimizer | `Compress.cpp` | ตรวจ resources/font และ linearization |
| K3 | working | Before/after visual and size preview, save copy | `CompressDialog.qml`, `Compressor.cpp` | เทียบพิกเซล ขนาด และไม่เปลี่ยนต้นฉบับ |
| K4 | partial | Batch compression with per-file result; no all-file visual comparison/estimate | `BatchDialog.qml`, `BatchPdf.cpp` | ตรวจความคืบหน้า/รายงานและ cancel |
| K5 | partial | Range/current/all and basic scaling; no complete booklet/poster/N-up controls | `PrintDialog.qml`, `PrintTask.cpp` | ตรวจ output และอุปกรณ์ duplex |
| K6 | blocked | Professional print production tools absent; engine/ICC qualification required | `PrintTask.cpp`, `PdfDocument.cpp` | ทดสอบ separations/spot/bleed/ICC |

### L

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| L1 | partial | Basic metadata; no accessibility report or tag editor | `MaintenanceDialog.qml`, `PdfDocument.cpp` | ตรวจ tag tree และ document language |
| L2 | missing | Reading order/headings/tables/alt editor | `ViewerScreen.qml`, `ReplaceText.cpp` | อ่าน tag order จาก output |
| L3 | partial | Qt keyboard/accessibility labels on selected controls; no full assistive tech qualification | `PButton.qml`, `LanguageSwitch.qml`, `ViewerScreen.qml` | เดิน keyboard และทดสอบ Narrator |
| L4 | blocked | PDF/UA assessment/repair requires qualified validator | `package.ps1` | ตรวจ PDF/UA ด้วย validator อิสระ |

### M

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| M1 | partial | Single-operation batch for compress/watermark/protect; no chained Action Wizard | `BatchDialog.qml`, `BatchPdf.cpp` | ทดสอบลำดับหลาย action |
| M2 | partial | Batch progress/cancel/results; no saved workflow presets | `BatchDialog.qml`, `BatchPdf.cpp` | บันทึก preset และทดสอบรันซ้ำ |
| M3 | missing | Watched folder/scheduler | `main.cpp`, `BatchPdf.cpp` | ทดสอบ file arrival, opt-in และ retries |
| M4 | partial | CLI opens PDF and offers diagnostic flags; no batch CLI | `main.cpp` | ตรวจ exit codes และผลไฟล์ |
| M5 | missing | Signed extension architecture | `CMakeLists.txt`, `main.cpp` | ปฏิเสธ unsigned extension |

### N

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| N1 | blocked | Cloud storage connectors require APIs and explicit auth; current recent paths are filesystem paths | `HomeScreen.qml`, `DocumentController.cpp` | ทดสอบ OAuth provider และ offline |
| N2 | blocked | No account/token/revocation/sync service | `Main.qml`, `DocumentController.cpp` | ทดสอบ secure token store และ revoke |
| N3 | blocked | No backend for provider share links | `LinkDialog.qml เป็น external-link confirmation` | ทดสอบ ACL/expiry ของลิงก์จริง |
| N4 | blocked | No collaboration/version backend | `SidePanel.qml`, `DocumentController.cpp` | ทดสอบ conflict/version/audit |
| N5 | blocked | No e-sign provider/backend | `FormDialog.qml`, `EditBar.qml` | ทดสอบลำดับ signer และ audit trail |

### O

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| O1 | blocked | No AI assistant model/provider | `main.cpp`, `ThaiOcr.cpp ทำเฉพาะ OCR` | คำตอบอ้างหน้าและตรวจ grounding |
| O2 | blocked | No multi-document semantic/summary pipeline | `DocumentController.cpp ทำค้นข้อความตรง` | ทดสอบ retrieval และ Thai |
| O3 | blocked | No natural-language action service | `ToolSearch.qml เป็นค้นคำสั่ง` | preview/confirm และผลจริงทุก action |
| O4 | blocked | No AI redaction suggestions; regex path exists | `RedactionBar.qml` | human approval และตรวจลบจริง |
| O5 | blocked | No AI workspace/share backend | `HomeScreen.qml` | จัดกลุ่มแหล่งอ้างอิงและสิทธิ์จริง |
| O6 | blocked | No generative content/rendering providers | `main.cpp` | ตรวจ output จริงและ consent |
| O7 | blocked | No provider/usage/token/PII controls | `PreferencesDialog.qml` | ทดสอบ opt-in, quota และ masking |
| O8 | blocked | No local language model; OCR model is specific to recognition | `ThaiOcr.cpp`, `CMakeLists.txt` | benchmark offline และคุมต้นทุน online |

### P

| ข้อ | สถานะ | สิ่งที่พบ | หลักฐาน | เกณฑ์ตรวจ |
|---|---|---|---|---|
| P1 | working | TH/EN translation, bundled Inter/Noto Sans Thai and language toggle | `Tr.qml`, `LanguageSwitch.qml`, `main.cpp`, `tst_ui_flow` | ทดสอบสลับภาษาและข้อความ UI |
| P2 | partial | Dark/light supported; no system-follow mode | `Theme.qml`, `PreferencesDialog.qml` | ทดสอบเปลี่ยน system theme |
| P3 | partial | Autosave recovery/dirty prompt/undo-redo; no universal backup or file-lock UX | `EditorModel.cpp`, `Main.qml` | crash/restart, locked file และรักษาต้นฉบับ |
| P4 | partial | Render cache/cancellation/large fixture; no configurable performance controls | `PageImageProvider.cpp`, `tst_perf` | วัด memory/scroll/cancel บนไฟล์ใหญ่ |
| P5 | partial | Diagnostics and tests; no opt-in crash reporting or consolidated support log | `main.cpp`, `scripts/build.ps1` | ตรวจ log ไม่มีข้อมูลเอกสาร |
| P6 | partial | Ctrl+K, context menus and shortcuts; no remapping/full a11y coverage | `ToolSearch.qml`, `ViewerScreen.qml` | keyboard, Narrator และ shortcut configuration |
| P7 | partial | Installer, associations and registry contract; rollback/install matrix requires owner test | `pdf.iss`, `package.ps1`, `clean-pc-checklist.md` | ทดสอบ clean Windows และ Open With |
| P8 | partial | Version/About/notices/release notes; suite hub handles updates, no PDF-local update channel | `AboutPdfDialog.qml`, `package.ps1`, `docs/suite-integration.md` | ตรวจ manifest/version/checksum และ hub |

## เกณฑ์ fixture และผลที่ยังต้องเก็บ

| ข้อจากแผน | หลักฐาน baseline | ช่องว่าง |
|---|---|---|
| 1 เอกสารจ้างงานไทย | `tst_pdfengine` และ `tst_editor` สร้าง PDF ไทยและตรวจข้อความ/พิกเซล | ไฟล์จริงผสมฟอนต์ฝังและไม่ฝัง รวม edit/export |
| 2 สแกนไทย 10 หน้า | `tst_output` ตรวจ OCR หน้าภาพหนึ่งหน้าและความเท่ากันของพิกเซล | ชุดสแกน 10 หน้าและวัดคุณภาพภาพก่อน/หลัง |
| 3 PDF 200+ หน้า | `tst_perf` 500/3000 หน้า, `tst_pdftools` merge 600 หน้า | รวม scroll/reorder/cancel ใน scenario เดียว |
| 4 ฟอร์มและ annotation | `tst_forms`, `tst_editor`, `tst_ui_flow` เปิดตรวจสำเนา | ตรวจทั้งสองอย่างในไฟล์เดียวหลัง restart |
| 5 เข้ารหัส | `tst_pdfengine`, `tst_output`, `tst_ui_flow` | permission enforcement ทุกคำสั่ง |
| 6 ลายเซ็น | มี guard ในบางเส้นทางเขียน | certificate, trust, invalidation fixtures |
| 7 Redaction | `tst_redaction` ตรวจ text/image/metadata/attachment | เพิ่มไฟล์จริงหลายโครงสร้าง |
| 8 Office/images | `tst_output` ครอบคลุม images/ZIP | DOCX/XLSX/PPTX ยังขาด converter |
| 9 Merge/split/reorder | `tst_pdftools`, `tst_organizer` ตรวจหน้าและ output | เพิ่มเอกสารหลาย source ที่มี link/form/outline ซับซ้อน |
| 10 Installer/Open With | ตรวจสคริปต์และเกตแพ็กเกจรุ่น 1.3.4 | ทดสอบติดตั้ง/ถอนบน Windows สะอาดโดยเจ้าของตาม AGENTS |

## ลำดับงาน

| Phase | งานถัดไปตามลำดับ |
|---|---|
| 0 | รัน regression baseline และจัดสถานะตามหลักฐานในตารางนี้ |
| 1 | รวม startup/เปิดไฟล์ในแอปให้ใช้ error/password flow เดียวกัน จากนั้น multi-open queue และทดสอบการกู้ draft เมื่อไฟล์ต้นทางเปลี่ยน ตามด้วยแท็บ/การนำทางและ fixture ไทย |
| 2 | สร้าง blank/image PDF, page operations ที่ขาด และตรวจ output ก่อนประเมิน converter/print preview |
| 3 | เพิ่มความสามารถแก้ native text ทีละชนิด เริ่มจาก fixture ไทยและฟอนต์ แล้วต่อด้วยวัตถุ/ลิงก์/แบบฟอร์ม |
| 4 | OCR fixture 10 หน้า, review/annotations และประเมิน certificate workflow |
| 5 | ตรวจ permission/signature guards ทุก writer และเพิ่ม redaction corpus |
| 6 | เปรียบเทียบและรายงาน PDF, accessibility/preflight validators และ batch presets |
| 7 | พิจารณา provider จริงและข้อกำหนด consent/credential ตามขอบเขตที่อนุมัติ |
| 8 | ทดสอบ performance/clean Windows, support diagnostics และ signed packaging เมื่อมีใบรับรอง |

## การตรวจสอบ

- Baseline 1.3.4 ใช้ toolchain ของ repo และรัน `ctest --test-dir apps/drivevault-pdf-pro/build/release --output-on-failure` กับบิลด์ที่มีอยู่ ผ่าน 12/12 suites

## Phase 1 — เริ่มจากการเปิดไฟล์

- `src/app/main.cpp` ส่งพาธแรกและรหัสผ่านจาก diagnostic argument ให้ `Main.openPath` หลัง QML พร้อม จึงใช้ dialog และ error state เดียวกับการเปิดไฟล์จากในแอป การเลือกหลายไฟล์เป็นงานถัดไป
- `src/ui/screens/ViewerScreen.qml` ตรวจว่าแท็บเดิมยังอยู่ก่อนคืนตำแหน่งหน้าใน callback ที่รันทีหลัง ปิดหรือสลับแท็บได้โดย callback เดิมไม่ไปเปลี่ยนมุมมองของเอกสารอื่น
- `tests/tst_ui_flow.cpp` ตรวจ password prompt, wrong password, valid password ผ่านกล่องเดิม และ valid password ที่ส่งผ่าน startup bridge รวมการปิดแท็บทันที ไม่มีการเปลี่ยนรูปแบบ settings หรือ recovery จึงไม่ต้องย้ายข้อมูล
- รันโปรแกรม Release ด้วย positional argument และโปรไฟล์ทดสอบใหม่ ตรวจสี่กรณี ได้แก่ PDF ปกติ, PDF เข้ารหัสโดยไม่ส่งรหัส, ไฟล์เสีย และ PDF เข้ารหัสที่ส่งรหัสถูกผ่าน `--password` ได้ exit code 0 สำหรับ screenshot mode ทั้งสี่กรณี
- `apps/drivevault-pdf-pro/scripts/build.ps1 -Test` รอบแรกผ่าน build และ 11/12 suites โดย `tst_ui_flow` พบ delayed callback หลังปิดแท็บ แก้เงื่อนไขตรวจแท็บแล้วรันซ้ำผ่าน 12/12 suites และ `scripts/check-agent-files.ps1` ตรวจครบ 25 โฟลเดอร์โดยไม่มีปัญหา

| ลำดับถัดไป | เกณฑ์ผ่าน |
|---|---|
| 1.2 เปิดหลายไฟล์ | picker และ drop เปิดตามลำดับ ไฟล์ติดรหัสหยุดรอได้ ยกเลิกหรือข้ามไฟล์เสียแล้วเปิดรายการถัดไป |
| 1.3 กู้ draft อย่างปลอดภัย | fixture เดิมกู้ได้ เมื่อไฟล์ต้นทางถูกแทนที่ต้องแจ้งก่อนนำ draft เก่ามาใช้ ทดสอบปิด/เปิดใหม่และต้นฉบับมีแฮชเดิม |
| 1.4 แท็บและการนำทาง | เปิดแท็บที่เพิ่งปิดกลับได้ คืนหน้า/ซูม และยืนยันก่อนทิ้งงานค้าง |
| 1.5 fixture ไทย | ทดสอบเอกสารไทย/อังกฤษแบบไม่ระบุตัวตน เทียบพิกเซล อักขระที่คัดลอก และความถูกต้องหลังหมุนหน้า |

## Phase 1 — รอบ 1.4.0

- `Main.qml` ใช้คิวร่วมของ file picker และ drag-and-drop เปิดตามลำดับ รอ password/recovery modal และแจ้งรายชื่อไฟล์ที่เปิดไม่สำเร็จหลังจบคิว การเลือกไฟล์เพื่อสั่งเครื่องมือเฉพาะยังเลือกได้หนึ่งไฟล์
- `EditorModel.cpp` บันทึกขนาดและเวลาแก้ไขของต้นทางพร้อม autosave ฉบับร่างเก่ายังอ่านได้และจะขอให้ผู้ใช้ยืนยันเมื่อข้อมูลต้นทางหายหรือไม่ตรง การตรวจนี้ใช้ metadata ของไฟล์ ไฟล์ที่ถูกแทนที่ด้วยขนาดและ timestamp เดิมอาจตรวจไม่พบ
- `ViewerScreen.qml` และ `Main.qml` ใช้เส้นทางปิดแท็บเดิมและบันทึกประวัติแท็บที่เพิ่งปิดสูงสุด 10 รายการใน session กด Ctrl+Shift+T เพื่อคืนแท็บตามตำแหน่ง/ซูมเดิม
- `tst_pdfengine` เพิ่มวันที่ จำนวนเงิน และการค้น/ระบุตำแหน่งอักษรไทยเมื่อหมุนหน้าใน fixture สังเคราะห์ ข้อจำกัดลำดับสระและวรรณยุกต์จากบางตัวสร้าง PDF ยังต้องตรวจด้วย fixture จริง
- รูปแบบ settings เดิมใช้ต่อได้ ข้อมูล autosave ใหม่เพิ่มฟิลด์เสริมใน JSON เดิม ไม่มี dependency ใหม่
- รัน `apps/drivevault-pdf-pro/scripts/build.ps1 -Test` รอบแรกพบว่า fixture ไทยแบ่งคำค้นเป็นหลายกรอบ เทสต์จึงวัดผิดเมื่อใช้กรอบแรกเพียงกรอบเดียว แก้เทสต์ให้วัดกรอบรวมแล้วรันซ้ำผ่าน 12/12 suites ตรวจการเปิดจากบิลด์ Release ด้วย `scripts/run-pdf-pro.bat` และ PDF ตัวอย่างโดยไม่ติดตั้ง
- เทสต์ UI ครอบคลุมฟังก์ชันคิวที่ picker และ drop เรียกร่วมกัน ส่วน Windows file picker และการลากจาก File Explorer ยังไม่ได้ทดสอบแบบ manual

## Phase 2 — รอบ 1.5.0

- **B1 สร้าง PDF เปล่า** เปิดจากหมวดแปลงไฟล์หรือ Ctrl+K เลือก A4, Letter, Legal หรือขนาดกำหนดเอง แนวตั้ง/แนวนอนและระยะขอบ
- **B3 สร้าง PDF จากรูป** เลือก PNG, JPEG หรือ BMP หลายรูป เลื่อนลำดับ ลบหรือหมุนก่อนบันทึก รูปอยู่กึ่งกลางหนึ่งรูปต่อหนึ่งหน้า รักษาสัดส่วนและอ่าน orientation ที่ฝังในไฟล์เมื่อ decoder รองรับ
- `CreatePdfDialog.qml` ใช้รูปแบบ popup เดิมและเปิดผลลัพธ์ในแท็บ `DocumentController.cpp` ใช้ worker และสัญญาณงานส่งออกที่มีอยู่ `CreatePdf.cpp` ใช้ Qt Image Reader และ QPdfWriter ที่มีอยู่แล้ว เขียนไฟล์ชั่วคราวในโฟลเดอร์ปลายทาง เปิดตรวจจำนวนหน้าด้วย PDFium แล้วจึงเปลี่ยนชื่อเป็นปลายทาง การยกเลิกหรือรูปเสียไม่ส่งมอบ PDF ค้าง และชื่อไฟล์ที่มีอยู่จะถูกปฏิเสธ
- ยังไม่มี Office/HTML import, TIFF/WebP ที่ยืนยันในแพ็กเกจ, การรวมแหล่งหลายชนิดหรือหน้าตัวอย่างก่อนพิมพ์ งานถัดไปใน Phase 2 คือเครื่องมือหน้าที่ขาดและการส่งออกข้อความ ก่อนประเมินตัวแปลง Office และ print preview งาน Phase 3 ต้องรอคำยืนยัน
- `apps/drivevault-pdf-pro/scripts/build.ps1 -Test` ผ่าน 12/12 suites ทดสอบเปิดคำสั่งจาก QML สร้างและเปิดผลจริง ตรวจขนาดกระดาษ จำนวนหน้า สีและตำแหน่งพิกเซลหลังสลับลำดับและหมุนรูป ตรวจชื่อซ้ำ รูปเสีย และการยกเลิก จากนั้นเปิดบิลด์ Release ด้วย `scripts/run-pdf-pro.bat` และตรวจคู่ไฟล์คำสั่ง agent ครบ 25 โฟลเดอร์
