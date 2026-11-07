# DriveVault Video Editor Pro — Professional Desktop Video Editor

> **Claude Code Master Development Prompt**  
> Product: **DriveVault Video Editor Pro**  
> Application ID (suggested): **com.drivevault.videoeditorpro**  
> Slogan: **Professional Power. Beautiful Simplicity.**  
> Platform: **Native Windows Desktop (Windows 10/11 x64)**  
> Primary language for communication: **ภาษาไทย**  
> UI Languages: **ไทย / English**

---

## 1. บทบาทและเป้าหมาย

คุณคือทีมวิศวกรรมซอฟต์แวร์ระดับมืออาชีพที่ต้อง **ออกแบบ พัฒนา ทดสอบ และส่งมอบโปรแกรมตัดต่อวิดีโอที่ใช้งานได้จริง** ชื่อ **DriveVault Video Editor Pro** โดยใช้ Adobe Premiere Pro และ DaVinci Resolve เป็น *แรงบันดาลใจด้านความสามารถและ workflow* แต่สร้างตัวตน แบรนด์ และงานออกแบบของตนเองทั้งหมด

หลักการผลิตภัณฑ์:

- **Professional:** รองรับงานตัดต่อแบบ Non-Linear Editing (NLE) หลายแทร็ก การมิกซ์เสียง เอฟเฟกต์ Keyframe และส่งออกวิดีโอ
- **Premium:** ดีไซน์หรูหรา สะอาด เรียบง่าย ตอบสนองไว และมีรายละเอียดแบบซอฟต์แวร์เชิงพาณิชย์
- **Approachable:** ผู้เริ่มต้นเข้าใจได้ง่าย แต่ผู้ใช้ระดับมืออาชีพทำงานได้คล่อง
- **AI-Assisted:** มี AI ช่วยถอดเสียง สร้างซับ ตรวจจับความเงียบ เสนอจุดตัด และปรับเสียง
- **Privacy-First:** ใช้งาน Offline เป็นหลัก และเปิดใช้โมเดล AI ในเครื่องได้
- **Reliable:** งานตัดต่อและการ Export ต้องทำงานจริง ไม่ใช่เพียงหน้าตาตัวอย่าง

### ข้อห้ามเด็ดขาด

- **Desktop Application เท่านั้น:** ห้ามสร้างเว็บไซต์, Web App, PWA, Mobile App หรือเว็บสำหรับ UI หลัก
- **ไม่ใช้ Electron, Tauri หรือ WebView** สำหรับแอปหลัก
- ไม่ต้องมี Web Server หรือ Cloud Backend สำหรับงานตัดต่อทั่วไป
- ห้ามคัดลอก Code, Assets, Logo, Trademark หรือหน้าตาที่เป็นทรัพย์สินของ Adobe/คู่แข่งโดยตรง
- ห้ามสร้างปุ่มที่ไม่มีการทำงานจริงแล้วแจ้งว่าเสร็จสมบูรณ์
- ห้ามอ้างว่า Build/Test/Package สำเร็จหากไม่ได้ดำเนินการจริง
- ห้ามนำงานหลาย Phase มาปะปนกันจนเกิดระบบขนาดใหญ่ที่ทดสอบไม่ได้

---

## 2. ทีมพัฒนาและความรับผิดชอบ

จำลอง Workflow ทีมที่ประกอบด้วย:

1. **Product Manager (PM):** กำหนด Requirement, Scope, Milestone และ Acceptance Criteria
2. **Business Analyst (BA):** วิเคราะห์การใช้งานจริงและ User Journey
3. **Software Architect (SA):** ออกแบบ Modular Architecture, Interfaces และ Data Flow
4. **UI/UX Designer:** ออกแบบ Design System, Interaction, Accessibility และ Responsive Desktop Layout
5. **Desktop Engineer:** พัฒนา C++/Qt/QML และ Native Windows Integration
6. **Media Engine Engineer:** พัฒนา Decode, Timeline, Playback, Audio Sync และ Rendering
7. **AI Engineer:** พัฒนา AI Service, Model Management และ Task Pipeline
8. **Persistence Engineer:** จัดการ Project Format, SQLite/JSON, Auto Save และ Recovery
9. **QA Engineer:** เขียน Tests, Performance Benchmarks และ Regression Tests
10. **Release Engineer:** สร้าง Installer, Uninstaller, Versioning และ Release Package
11. **Security & License Reviewer:** ตรวจสอบ Dependencies, Codec, Media Inputs และ Third-Party Licenses

ทุกบทบาทต้องทำงานตามสถาปัตยกรรมเดียวกันและรายงานข้อจำกัดอย่างตรงไปตรงมา

---

## 3. เทคโนโลยีหลัก

| ส่วนประกอบ | เทคโนโลยีที่แนะนำ |
|---|---|
| ภาษาโปรแกรม | C++20 |
| Native UI | Qt 6 / Qt Quick / QML / Qt Quick Controls 2 |
| Media Processing | FFmpeg |
| Editing / Compositing | MLT Framework (ประเมินความเข้ากันได้และ License ก่อนใช้จริง) |
| Speech-to-Text | whisper.cpp |
| AI Inference | ONNX Runtime |
| Project Index/Cache | SQLite |
| Project/Settings Serialization | JSON พร้อม Schema Version |
| Build | CMake |
| Package Management | vcpkg หรือระบบที่เหมาะสม |
| Windows Installer | Inno Setup |
| Tests | Qt Test / CTest และ Integration Tests |
| Version Control | Git |

**ข้อกำหนดทางเทคนิคเพิ่มเติม**

- เริ่มที่ Windows 10/11 x64 ก่อน และแยก Platform Services เพื่อเปิดทางให้ macOS/Linux ในอนาคต
- แยก Core, Media, UI, AI, Persistence และ OS Integration เป็นโมดูล
- ใช้ FFmpeg/MLT ผ่าน Adapter/Service Interfaces ไม่เรียกโดยตรงกระจัดกระจายใน QML
- ตรวจสอบความเข้ากันได้ของ FFmpeg/MLT/Qt กับ Compiler/ABI ก่อนเลือกเวอร์ชัน
- ประเมิน License ของ Qt, FFmpeg, MLT, AI Models และ Codecs **ก่อน** แจกจ่าย Installer; อย่าสมมติว่าทุกชุดเป็น Commercial-Friendly เสมอ
- Feature Detection ต้องอิงความสามารถจริงของ GPU/Driver/Codec และมี CPU Fallback
- ห้าม Download หรือติดตั้ง System-wide Dependencies โดยไม่ทราบผลกระทบหรือไม่มีสิทธิ์

---

## 4. Product Design & Visual Identity

### 4.1 Design Direction

DriveVault Video Editor Pro ต้องให้ความรู้สึกเป็น **Premium Creative Desktop Software**: Modern, Minimal, Elegant, Efficient, High Contrast, Clean Typography และมี Motion ที่นุ่มนวลพอเหมาะ

ไม่ใช้ดีไซน์แบบ Windows Forms รุ่นเก่า ไม่ใส่ Gradient/Glow จนรก ไม่ทำ Timeline คล้ายหน้าเว็บ Dashboard

### 4.2 Theme Colors

**Default: Premium Dark**

| Token | Color |
|---|---|
| Main Background | `#101114` |
| Secondary Background | `#181A20` |
| Surface | `#20232B` |
| Elevated Surface | `#292D36` |
| Primary Accent | `#8B5CF6` |
| Accent Hover | `#A78BFA` |
| Primary Text | `#F8FAFC` |
| Secondary Text | `#A1A1AA` |
| Border | `#343844` |
| Success | `#22C55E` |
| Warning | `#F59E0B` |
| Error | `#EF4444` |

รองรับ **Light Theme** และ **Accent Color Customization**

### 4.3 Typography & DPI

- อังกฤษ: Inter หรือฟอนต์ที่อนุญาตให้แจกจ่าย
- ไทย: Noto Sans Thai หรือฟอนต์ที่อนุญาตให้แจกจ่าย
- ฟอนต์ภาษาไทยต้องตัดคำ/วรรณยุกต์ได้ถูกต้อง
- รองรับ Unicode, Thai file path, High DPI และ Display Scaling อย่างเหมาะสม
- ควบคุม Minimum Size ของ Controls เพื่อให้อ่านง่าย

### 4.4 UI Components

สร้าง Reusable QML Components เช่น Buttons, Icon Buttons, Splitters, Panel Headers, Tabs, Context Menus, Dropdowns, Tooltips, Search, Dialogs, Toasts, Progress, Empty States, Loading States, Sliders และ Audio Meter

Animation ที่อนุญาต: Hover, Focus, Fade, Panel Transitions และ Smooth Scrolling โดย **ห้ามลด Playback/Timeline Performance**

### 4.5 Accessibility

- Focus Indicators ชัดเจน
- Keyboard Navigation ที่สำคัญ
- รองรับการขยายขนาด UI
- Text Contrast เพียงพอ
- Tooltip และ Labels ชัดเจน
- Status ไม่สื่อด้วยสีอย่างเดียว

---

## 5. หน้าจอและ User Journey

### 5.1 Home / Welcome Screen

เมื่อเปิดแอปต้องพบหน้าจอ Home ที่มี:

- DriveVault Video Editor Pro Branding
- **New Project**
- **Open Project**
- **Recent Projects** พร้อมชื่อ, Path, Last Opened, Thumbnail (เมื่อมี)
- Quick Start / Getting Started
- Settings
- About / Version
- Empty States และ Error States ที่สวยงาม

Flow: Home → New Project → Project Configuration → Editor → Save → Export

### 5.2 Main Editor Shell

**Top Menu Bar**: File, Edit, Clip, Sequence, Effects, View, Window, Help  
**Workspace Switcher**: Home, Media, Edit, Color, Audio, Effects, Export

**Left: Project Media Panel**
- Import Files / Folder
- Media Bins/Folder Tree
- Thumbnails / List View
- Search, Sort, Metadata
- Source Clip Preview
- Drag & Drop จาก Explorer

**Center: Program Monitor**
- Frame-accurate Playback, Play/Pause, Seek/Scrub
- Previous/Next Frame
- Timecode
- Preview Resolution / Quality
- Fullscreen Monitor
- Transform Handles, Safe Areas (เมื่อรองรับ)

**Right: Inspector / Properties**
- Position, Scale, Rotation, Opacity, Crop, Speed
- Audio Volume
- Effects Parameters
- Keyframe Editor

**Bottom: Multitrack Timeline**
- Video/Audio Tracks, Ruler, Playhead, Markers
- Clip Thumbnails, Waveforms
- Track Visibility/Mute/Lock
- Snap, Zoom, Horizontal/Vertical Scroll
- Multiselect, Context Menus

ทุก Panel ต้อง Resize ได้ พร้อม Save/Restore Workspace Layout; เผื่อโครงสร้างรองรับ Multi-Monitor ในอนาคต

### 5.3 Settings Screen

ออกแบบเป็น Premium Settings Window ที่จัดหมวดหมู่ชัดเจน:

- **General:** Language, Theme, Accent, Startup, Recent Projects
- **Appearance:** Scaling, Font Size, Animations, Workspace
- **Performance:** Hardware Acceleration, Preview Quality, Cache Location/Size, Memory
- **Editing:** Default FPS, Snapping, Auto Save, Default Project Location
- **Audio:** Output Device, Preview Volume, Latency
- **AI:** Enable, Model Manager, Device, Privacy, Local Storage
- **Export:** Format, Presets, Quality, Output Folder
- **About:** Version, Build Number, OS, License, Third-Party Notices

ทุกการเปลี่ยนค่าต้อง Persist ได้ และแจ้งอย่างชัดเจนหากต้อง Restart

---

## 6. Project Management

ฟีเจอร์หลัก:

- New, Open, Save, Save As
- Recent Projects
- Project Settings: Resolution, Timebase/FPS, Audio Sample Rate
- Auto Save (Interval Configurable)
- Crash Recovery / Backup
- Media Relink เมื่อไฟล์ถูกย้าย
- Save/Load Workspace Layout

ใช้นามสกุลไฟล์โครงการ **`.dvproj`** (DriveVault Video Editor Project) พร้อม File Format Version และ Migration Strategy

- Persist โครงสร้าง Timeline, Media References, Effects และ Settings ให้ชัดเจน
- Save แบบ Atomic Write หรือ Transactional Approach
- ไม่ฝังวิดีโอขนาดใหญ่ไว้ใน Project File โดยไม่จำเป็น
- จัดการ Relative/Absolute Paths อย่างรอบคอบ
- ห้ามทำให้ Project เสียหายหากแอป Crash ขณะ Save

---

## 7. Media Import & Preview

ทดสอบอย่างน้อย:

- **Video:** MP4, MOV, MKV, AVI, WebM
- **Audio:** MP3, WAV, AAC, FLAC
- **Image:** PNG, JPG, WebP

ความเข้ากันได้จริงขึ้นกับ FFmpeg Build, Enabled Codecs และสิทธิ์การแจกจ่าย

ต้องมี:

- Import ผ่าน File Picker และ Drag & Drop
- Media Probe (Duration, FPS, Dimensions, Streams, Codec)
- Asynchronous Thumbnail Generation
- Waveform Generation
- Background Tasks พร้อม Cancel
- แจ้ง Error เมื่อไฟล์เสียหรือ Unsupported
- Correct A/V Timestamp Handling
- Handling Variable Frame Rate Sources

---

## 8. Core Non-Linear Editing (NLE)

### 8.1 Timeline Operations

- Add/Move/Reorder Clips
- Multiple Video/Audio Tracks
- Trim Start/End
- Split at Playhead
- Insert / Overwrite
- Ripple Edit
- Roll Edit
- Slip / Slide Edit (หลัง MVP)
- Copy / Paste / Duplicate
- Group / Ungroup
- Multiselect
- Snapping / Markers
- Undo / Redo
- Zoom to Fit / Horizontal Zoom
- Track Lock / Mute / Visibility

**Timing Requirements:** ออกแบบ Timebase ที่ Frame-Accurate โดยใช้ Rational Time หรือ Frame Index ตามมาตรฐานที่เหมาะสม ห้ามใช้ Floating-Point Seconds เพียงอย่างเดียวจนเกิด Drift

**Undo/Redo:** ใช้ Command Pattern หรือ Transactional Edit Operations ที่ย้อนกลับได้

### 8.2 Playback Engine

- Play/Pause, Seek, Step Frame
- Smooth Scrubbing
- Audio/Video Synchronization
- Preview Rendering ของ Timeline Composite จริง
- Background Decode และ Frame Queue
- Caching/Prefetch
- Proxy Editing ใน Phase Optimization
- UI Thread ต้องไม่ถูก Block

### 8.3 Effects & Transitions

**Effects:** Brightness, Contrast, Saturation, Exposure, Temperature, Tint, Blur, Sharpen, Crop, Transform, Opacity, Vignette

**Transitions:** Cross Dissolve, Fade In/Out, Dip to Black, Wipe, Slide, Zoom

Effects/Transitions ต้องส่งผลทั้ง **Preview และ Final Export** อย่างสอดคล้องกัน

### 8.4 Keyframes

รองรับ Position, Scale, Rotation, Opacity, Volume และ Effects Parameters พร้อม Interpolation อย่างน้อย Linear และ Ease In/Out

### 8.5 Text / Titles / Subtitles

- Text Overlays และ Title Presets
- Thai / English Fonts
- Alignment, Fill, Stroke, Shadow
- Position/Scale/Animation
- Subtitle Track และ Subtitle Timing
- SRT Import/Export เมื่อพัฒนาถึงขั้นที่เกี่ยวข้อง

### 8.6 Audio

- Waveform, Volume, Gain, Fade In/Out, Mute
- Multi-track Audio Mixing
- EQ และ Basic Audio Effects
- Noise Reduction ตามความสามารถที่พัฒนาจริง
- Audio Processing ไม่ทำงานหนักบน Main UI Thread

### 8.7 Color

- White Balance, Exposure, Contrast
- Lift/Gamma/Gain
- RGB Curves
- LUT Import
- Preview/Export Color Consistency ภายในขอบเขต Color Pipeline ที่รองรับ

---

## 9. AI Editing Assistant

AI เป็น **Optional Module** แยกจาก Editing Core; หากไม่มี AI Model แอปต้องตัดต่อพื้นฐานได้

### 9.1 Speech-to-Text / Auto Captions

ใช้ whisper.cpp เพื่อ:

- Transcribe ภาษาไทย/อังกฤษ
- แสดง Progress/Cancel
- Generate Timestamped Segments
- สร้าง Editable Subtitle Track
- Export SRT
- Model Manager: Download/Import/Delete/Select Model

ต้องแจ้งข้อจำกัดด้านความแม่นยำและรองรับการแก้ข้อความด้วยมือ

### 9.2 Silence Detection

- วิเคราะห์ช่วงเงียบตาม Threshold/Minimum Duration
- Highlight บน Timeline
- Preview Suggested Cuts
- Apply หลังผู้ใช้ยืนยัน
- Undo ได้เสมอ

### 9.3 Smart Cut Suggestions

- Scene Change Detection
- Audio Activity Analysis
- Silence Detection
- Suggested Edit Points
- ไม่ตัดวิดีโอเองโดยไม่มีการยืนยัน

### 9.4 Object Tracking

- Select Subject/Region
- Track Across Frames
- Produce Tracking Data
- Link to Effect/Transform อย่างมีเหตุผล
- ประเมิน ONNX/OpenCV/อื่น ๆ และข้อกำหนดของ Model ก่อนเลือก

### 9.5 AI Audio Enhancement

- Background Noise Reduction
- Speech Enhancement
- CPU Fallback และ Progress/Cancel

### 9.6 Natural Language AI Assistant

ตัวอย่างคำสั่ง:

- “สร้างซับไทยให้คลิปนี้”
- “หาช่วงที่ไม่มีเสียงพูด”
- “แนะนำจุดตัดตรงที่เปลี่ยนฉาก”
- “ปรับเสียงพูดให้ชัดขึ้น”

AI ต้องแปลงคำสั่งเป็น **Validated Typed Commands** ผ่าน Tools/Services ภายในเท่านั้น ห้ามสั่ง Shell หรือแก้ไฟล์นอก Project ตามอำเภอใจ

UI ต้องระบุ Status: Available, Model Required, Unsupported Hardware หรือ Not Implemented อย่างตรงไปตรงมา

---

## 10. Export / Rendering

สร้าง Export Workspace ที่มี:

- Output Name / Directory
- Format, Codec, Resolution, Frame Rate
- Video/Audio Bitrate
- Presets: 720p, 1080p, 1440p, 4K, Custom
- Hardware/Software Encoder Selection เมื่อรองรับ
- Render Progress, Cancel, Errors, Output Validation

เริ่มที่ **MP4/H.264 + AAC** เฉพาะเมื่อ FFmpeg Build และ License/Codec Rights เอื้ออำนวย แล้วค่อยขยาย H.265/AV1

**Acceptance:** วิดีโอที่ Export ต้องมีภาพ/เสียง/ระยะเวลา/เอฟเฟกต์ตรงกับ Sequence ตาม Test Cases ไม่ใช่เพียง Copy Original Source

---

## 11. Performance & Hardware Acceleration

- Background Media Probe, Decode, Thumbnail และ Waveform
- Frame Cache / Prefetch
- Timeline Render Virtualization
- Proxy Editing สำหรับวิดีโอความละเอียดสูง
- Quality Switching เช่น Full / Half / Quarter
- Hardware Decode/Encode ตาม Availability
- ตรวจจับ NVIDIA NVDEC/NVENC, Intel Quick Sync และ AMD Hardware Encoder ตาม Driver/Codec จริง
- CPU Fallback
- Memory/Cache Limit
- หลีกเลี่ยง Memory Leak, UI Freeze และ A/V Drift
- Performance Metrics สำหรับ Playback FPS, Dropped Frames และ Export Throughput

---

## 12. Thai / English Localization

รองรับ **ไทย (th-TH)** และ **English (en-US)** ครบทั้งแอป:

- แปลข้อความด้วย Qt Translation System (เช่น Qt Linguist/.ts/.qm) หรือระบบที่เหมาะสม
- ห้าม Hardcode ข้อความแสดงผลที่ควรแปล
- Language Selector ใน Settings
- สามารถเปลี่ยนภาษาโดยไม่ต้องติดตั้งใหม่
- Reload UI Translation ณ Runtime ถ้าเป็นไปได้ หรือแจ้ง Restart Requirement
- Unicode Path และ Unicode Filenames
- ทดสอบเมนู, Dialog, Error, Tooltips, Installer และ Uninstaller
- ฟอนต์ไทยแสดงสระ/วรรณยุกต์และการตัดคำอย่างถูกต้อง

---

## 13. Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+N | New Project |
| Ctrl+O | Open Project |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save As |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z | Redo |
| Ctrl+C / Ctrl+V | Copy / Paste |
| Space | Play / Pause |
| Ctrl+K | Split at Playhead |
| Delete | Delete Selected Clip |
| + / - | Timeline Zoom |

ป้องกัน Shortcut Conflicts และออกแบบ Custom Shortcut Settings ในระยะต่อไป

---

## 14. Native Windows Installer & Uninstaller

ใช้ **Inno Setup** หรือระบบติดตั้ง Native Windows ที่เหมาะสม

### Installer Experience: Premium

ออกแบบ Branding/Visual Style ให้เข้ากับ DriveVault Video Editor Pro โดยติดข้อจำกัดจริงของ Installer Framework อย่างเหมาะสม

Pages:

1. Welcome
2. Language (ไทย / English)
3. License Agreement
4. Install Directory
5. Optional Components
6. Installation Progress
7. Installation Complete / Launch App

Installer ต้อง:

- ตรวจสอบ OS/Architecture/Disk Space
- Include Qt Runtime Plugins, Media Libraries และ Assets ที่จำเป็น
- Create Start Menu / Optional Desktop Shortcut
- Register Uninstaller
- เลือก Install Scope ที่เหมาะสมกับสิทธิ์ผู้ใช้
- แสดง Version และ Publisher Info ที่ถูกต้อง
- ตรวจสอบ Dependency Licenses และ Notices

### Uninstaller

- ถอน Program Files, Shortcuts และ Registry Entries ที่สร้างเอง
- **ไม่ลบ Project ของผู้ใช้โดยอัตโนมัติ**
- ให้ตัวเลือกแยกสำหรับลบ Settings/Cache โดยอธิบายผลกระทบชัดเจน
- ห้ามลบ Shared Runtime/Dependencies ของแอปอื่น
- ทดสอบ Install → Launch → Uninstall → Reinstall

**Expected Artifact:** `DriveVault-Video-Editor-Pro-Setup-x64.exe` เฉพาะหลัง Build และ Verify File จริง

สามารถสร้าง **Prototype Installer** ตั้งแต่ Phase 1 และทำ Production Installer ใน Phase 6

---

## 15. Architecture & Repository Structure

ใช้ Modular Architecture และแยกขอบเขตอย่างชัดเจน:

- Presentation (QML / UI Components)
- Application Services
- Domain (Timeline/Project Model)
- Media Engine Adapters
- Playback / Render Services
- AI Services
- Persistence / Serialization
- Platform Integration

โครงสร้างเบื้องต้น:

```text
drivevault-video-editor-pro/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── roadmap.md
│   ├── ui-design-system.md
│   ├── media-pipeline.md
│   └── third-party-licenses.md
├── cmake/
├── src/
│   ├── app/
│   ├── core/
│   ├── ui/
│   ├── project/
│   ├── media/
│   ├── timeline/
│   ├── playback/
│   ├── effects/
│   ├── audio/
│   ├── color/
│   ├── export/
│   ├── ai/
│   ├── localization/
│   ├── settings/
│   ├── persistence/
│   └── platform/
├── resources/
│   ├── icons/
│   ├── fonts/
│   ├── themes/
│   ├── translations/
│   └── templates/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── performance/
├── installer/
│   └── windows/
├── scripts/
└── third_party/
```

โครงสร้างนี้ปรับเปลี่ยนได้หากมีเหตุผลทางวิศวกรรม โดยต้องบันทึกเหตุผลในเอกสาร

---

## 16. Safety, Reliability & Licensing

- Validate/Probe Media ก่อน Process
- รองรับ Corrupted/Unsupported Files และ Missing Codecs
- ป้องกัน Path Traversal/Invalid Paths
- ป้องกัน Command Injection จาก AI หรือ File Names
- จำกัดสิทธิ์การเขียนไฟล์ใน Temp/Cache
- Handle GPU Failure, Out-of-Memory, Disk Full, Device Change
- Background Cancellation และ Safe Shutdown
- Crash Logging แบบไม่เก็บข้อมูลส่วนตัวเกินจำเป็น
- Recover Unsaved Work และไม่ทับ Project ต้นฉบับโดยไม่ตั้งใจ
- ตรวจสอบ LGPL/GPL/Commercial Obligations ของ Qt, FFmpeg, MLT, Codec และ Models
- จัดทำ Third-Party License Notices และ Dependency Manifest

---

## 17. Testing & Acceptance Criteria

ต้องมี Unit / Integration / Performance Tests อย่างเหมาะสม และทดสอบ End-to-End ตาม Flow:

1. เปิดโปรแกรม Native Windows
2. สร้าง Project
3. Import Video และ Audio
4. Preview Video
5. ลาก Clip ลง Timeline หลาย Track
6. Trim และ Split
7. เพิ่ม Effect และ Transition ที่รองรับ
8. Save Project
9. ปิดและเปิด Project อีกครั้ง
10. Export วิดีโอจริง
11. ตรวจ Output Duration, Resolution, Frame Rate และ Audio
12. เปลี่ยน UI ภาษาไทย/อังกฤษ
13. ทดสอบ Installer/Uninstaller บน Windows

**Definition of Done** ต่อฟีเจอร์:

- Function ทำงานจริง
- ไม่มี Compile Error ใหม่
- มี Test Coverage ระดับเหมาะสมกับความเสี่ยง
- Error Handling ครบ
- UX State (Loading/Empty/Error/Success) ชัดเจน
- Document/README อัปเดต
- รายงานสิ่งที่ทดสอบจริงและสิ่งที่ยังไม่ได้ทดสอบ

---

## 18. Roadmap และลำดับการพัฒนา

### Phase 1 — Native Desktop Foundation

- Repository, Build, Qt Main Window
- Premium UI Design System
- Home Screen
- Editor Layout (Panel Dock/Split/Resize)
- Theme, Localization, Settings
- Logging, Basic Persistence
- Build/Run จริง
- ทดลองสร้าง Installer หาก Environment พร้อม

**Deliverable:** แอป Desktop ที่เปิดได้จริง พร้อม Design System และหน้าจอหลัก ไม่อ้างว่าตัดต่อได้หาก Media Engine ยังไม่พร้อม

### Phase 2 — Functional Editing MVP

- Real Media Import/Preview
- Playback + Audio Sync
- Basic Video/Audio Tracks
- Move/Split/Trim
- Project Save/Load
- Undo/Redo
- Export MP4 ทำงานจริง

**Deliverable:** สามารถ Import → Edit → Save → Reopen → Export ได้จริง

### Phase 3 — Professional Tools

- Advanced Timeline Tools
- Effects / Transitions
- Keyframes
- Text / Titles
- Color Correction
- Audio Mixing
- Advanced Export Presets

### Phase 4 — AI Features

- Speech to Text
- Auto Subtitles
- Silence Detection
- Smart Cuts
- AI Audio Enhancement
- AI Assistant
- Object Tracking (เมื่อ Foundation พร้อม)

### Phase 5 — Performance & Reliability

- GPU Acceleration
- Proxy Editing
- Cache Optimization
- Large/Long Project Tests
- A/V Sync Accuracy
- Crash Recovery
- Performance Profiling

### Phase 6 — Installer & Product Release

- Production Installer/Uninstaller
- Runtime Packaging
- Versioning / Release Notes
- Clean Machine Testing
- Third-Party License Audit
- Documentation

**สำคัญ:** ทำงานทีละ Phase; ฟีเจอร์ที่ทำไม่เสร็จให้ทำเครื่องหมายตรงไปตรงมา ห้าม Mock แล้วอ้างว่า Complete

---

## 19. Development Rules สำหรับ Claude Code

1. **สื่อสารเป็นภาษาไทยเป็นหลัก**; ใช้ English สำหรับ Class, Method, Variable และ Technical Identifiers
2. เริ่มจากตรวจสอบ Repository และ Environment ก่อนสร้าง/แก้ไฟล์
3. ปฏิบัติตาม Architecture และ Coding Standards ของโปรเจกต์
4. พัฒนาเฉพาะ Scope ปัจจุบัน และแยกงานนอก Scope ไปเป็น Backlog
5. อย่าลบหรือเขียนทับงานผู้ใช้โดยไม่จำเป็น
6. เขียน Clean, Modular, Testable Code
7. อย่าปล่อย Heavy Media/AI Tasks ทำงานบน UI Thread
8. ทุก Media Transformation ต้องสะท้อนผลทั้ง Preview และ Export หรือระบุข้อจำกัด
9. Build และแก้ Compile Errors หลัง Implement
10. Run และตรวจหน้าจอ Native จริงเมื่อ Environment รองรับ
11. ถ้าไม่สามารถ Run GUI/Build Windows Installer ใน Environment นั้นได้ ให้รายงานตามตรงและจัดเตรียมคำสั่งทดสอบบน Windows
12. ไม่อ้างว่า QA ผ่านถ้าไม่ได้ทดสอบจริง
13. ไม่ใช้ Fake AI, Fake Timeline หรือ Fake Export เป็นฟีเจอร์ที่ประกาศว่าเสร็จ
14. ทุก UI Text ที่ต้องแปลให้ผ่าน Localization Layer
15. ตรวจสอบ Third-Party License ก่อน Package/Distribute
16. เขียน README พร้อม Prerequisites, Build, Run และ Test
17. สรุป Modified Files, Tests, Issues และ Next Steps หลังงานแต่ละรอบ
18. เมื่อพบข้อจำกัดเชิงเทคนิค ให้เสนอทางเลือกที่ปฏิบัติได้จริงพร้อม Trade-offs
19. อย่าเริ่ม Phase ถัดไปโดยละเลย Critical Errors ของ Phase ปัจจุบัน
20. เน้นคุณภาพของ Core Editing มากกว่าปริมาณ Feature

---

## 20. คำสั่งเริ่มงาน (Execute Now)

**เริ่มทำ Phase 1 ทันที** โดยให้ดำเนินการตามลำดับ:

1. ตรวจสอบระบบปฏิบัติการ, Compiler, CMake, Qt 6, FFmpeg, MLT, vcpkg และ Inno Setup ที่มีอยู่จริง
2. วิเคราะห์ความเข้ากันได้ของ C++/Qt/Media Dependencies และ License ที่เกี่ยวข้อง
3. สรุป Architecture และ UI Design Decisions แบบกระชับ
4. สร้าง Project Structure และ CMake Build
5. พัฒนา Native Qt Application พร้อม Premium Dark Design System
6. พัฒนา Home, Editor Shell, Resizable Panels, Settings และ Theme
7. เพิ่ม Localization ไทย/อังกฤษ และ Translation Workflow
8. เพิ่ม Settings Persistence และ Error/Logging Foundation
9. Build, Run และทดสอบ GUI ใน Environment ที่รองรับ
10. จัดทำ README และ Build/Run Instructions
11. สร้าง Installer เวอร์ชันทดสอบหาก Windows Toolchain พร้อม
12. สรุปสถานะและรายการที่ต้องทำใน Phase 2

**ห้ามหยุดเพียงขั้นตอนเขียนแผนหรือเอกสาร หากสามารถพัฒนา Source Code ได้ ให้ลงมือ Implementation จริง**

**รูปแบบรายงานหลังจบงาน:**

- Completed
- Modified / Created Files
- Build Results
- Test Results
- Known Issues / Limitations
- Next Phase Tasks

---

## 21. ตัวอย่าง Prompt สั่งงานหลัง Phase 1

```text
ดำเนินการพัฒนา DriveVault Video Editor Pro Phase 2 ตาม Architecture และมาตรฐานเดิมของโปรเจกต์

เน้น Media Import, Video Preview, Multitrack Timeline, Split, Trim,
Project Save/Load, Undo/Redo และ Export MP4 ที่ทำงานได้จริง

ให้ Build, Run, Test และแก้ไขข้อผิดพลาดก่อนสรุปผล
ห้ามแก้ไขส่วนอื่นที่ไม่เกี่ยวข้องโดยไม่จำเป็น
สื่อสารและสรุปผลเป็นภาษาไทย
```

---

## 22. Product Success Criteria

DriveVault Video Editor Pro จะถือว่าเป็นผลิตภัณฑ์ระดับใช้งานได้จริงเมื่อผู้ใช้สามารถ:

- ติดตั้งและถอนติดตั้งบน Windows ได้อย่างถูกต้อง
- เปิดโปรแกรมและใช้งาน UI ไทย/อังกฤษได้
- สร้าง/บันทึก/เปิด Project ได้โดยข้อมูลไม่หาย
- นำเข้าวิดีโอ/เสียงและดู Preview ได้ถูกต้อง
- ตัดต่อบน Timeline หลาย Track ได้โดยไม่กระตุกเกินความจำเป็น
- Export ผลลัพธ์ที่ถูกต้องทั้งภาพ เสียง และลำดับเวลา
- ใช้งาน AI Features ที่ประกาศว่าพร้อมใช้งานได้จริง
- ทำงานได้อย่างเสถียร มี Error Handling และ Crash Recovery ที่เหมาะสม

**เป้าหมายสุดท้าย: ส่งมอบ Native Professional Video Editor ที่สวยแบบ Premium แต่มีคุณภาพการใช้งานและความน่าเชื่อถือจริง ไม่ใช่เพียง Demo UI**
