```markdown
# لانداور

> كل مسح لبت واحد من المعلومات يستهلك طاقة مقدارها  
> $$E = kT \ln(2)$$  
> والـ firmware هو أقرب ما تكون إلى هذا القانون الفيزيائي.  
> — مبدأ لانداور

إصدار سطر الأوامر لبرنامج RWEverything. يعمل عبر نواة ويندوز + واجهة أوامر في وضع المستخدم، لتمكين وكيل الذكاء الاصطناعي من التعامل المباشر مع مسجلات أجهزة PCIe. تم الاستغناء عن الواجهة الرسومية، وجميع المخرجات قابلة للمعالجة (grep/parse)، وجميع الأخطاء يتم الإبلاغ عنها بشكل صريح.

اللغة العربية | [English](README_EN.md)

---

## البدء السريع

```bat
:: 1. تفعيل وضع التوقيع التجريبي (يتطلب إعادة تشغيل)
bcdedit /set testsigning on

:: 2. انقر بزر الماوس الأيمن على install.bat → تشغيل كمسؤول
::    يتم تلقائياً: ترجمة التعريف → ترجمة واجهة الأوامر → توقيع → تثبيت → تشغيل

:: 3. التحقق
landauer driver status
landauer pci list
```

إلغاء التثبيت:
```bat
:: انقر بزر الماوس الأيمن على uninstall.bat → تشغيل كمسؤول
```

---

## ملخص الأوامر

```
landauer pci list                             عرض جميع أجهزة PCI
landauer pci info <bdf>                       تفاصيل الجهاز (BAR, Cap, Class)
landauer pci cfg read <bdf> <off> [w]        قراءة مساحة التهيئة
landauer pci cfg write <bdf> <off> <val> [w] كتابة مساحة التهيئة
landauer pci cap list <bdf>                  عرض قائمة الإمكانيات (Capability)
landauer pci cap find <bdf> <id>             البحث عن إمكانية معينة
landauer pci ext-cap list <bdf>              عرض الإمكانيات الموسعة
landauer pci bar info <bdf> <idx>            عرض عنوان/حجم BAR
landauer pci bar map <bdf> <idx> [cache]     تعيين BAR → مقبض
landauer pci bar read <handle> <off> [w]     قراءة من BAR (MMIO)
landauer pci bar write <handle> <off> <v> [w] كتابة إلى BAR (MMIO)
landauer pci bar dump <handle> <off> <len>   تفريغ محتويات MMIO (stdout/ملف)
landauer pci bar unmap <handle>              تحرير تعيين BAR
landauer driver status                       التحقق من حالة التعريف
```

صيغة BDF: `bus:dev.func` بالنظام الست عشري، مثل `00:1f.6`

---

## المتطلبات

| البند | الشرح |
|----|------|
| نظام التشغيل | Windows 10/11 x64 |
| الصلاحيات | مسؤول (Administrator) |
| المترجم | Visual Studio 2022 (يكفي BuildTools) |
| SDK | Windows 10 SDK (يتضمن ملفات DDK) |
| التوقيع | وضع التوقيع التجريبي (`bcdedit /set testsigning on`) |

---

## هيكل المشروع

```
landauer/
├── ARCHITECTURE.md      # التصميم المعماري — بروتوكول الاتصال، نمط المزوّد، خريطة التوسع
├── COMMANDS.md          # دليل الأوامر الكامل (للوكلاء الذكيين)
├── README.md            # هذا الملف
├── protocol.h           # بروتوكول مشترك بين التعريف وواجهة الأوامر
├── install.bat          # تثبيت بنقرة واحدة
├── uninstall.bat        # إلغاء تثبيت بنقرة واحدة
│
├── driver/              # تعريف نواة نمط NT التقليدي
│   ├── driver.c         # DriverEntry + توزيع IRP
│   ├── dispatch.c       # تحليل الأوامر + توزيع على المزوّدات
│   ├── pci_provider.c   # مزوّد مساحة تهيئة PCI و BAR
│   ├── bar_table.c/h    # جدول تعيين BAR (MmMapIoSpace + KSPIN_LOCK)
│   ├── provider.h       # تعريف واجهة المزوّد
│   ├── landauer.inf     # معلومات تثبيت التعريف
│   ├── landauer.sln     # حل Visual Studio
│   ├── landauer.vcxproj # ملف مشروع Visual Studio
│   └── make.bat         # ترجمة باستخدام cl.exe مباشرة
│
└── cli/                 # واجهة أوامر طور المستخدم
    ├── main.c           # نقطة الدخول + توجيه الأوامر الفرعية
    ├── cmd_pci.c/h      # تنفيذ جميع أوامر PCI الفرعية
    ├── driver_if.c/h    # تغليف DeviceIoControl
    ├── format.c/h       # تنسيق المخرجات
    ├── landauer.vcxproj # ملف مشروع Visual Studio
    └── make.bat         # ترجمة باستخدام MSBuild
```

---

## أمثلة استخدام الوكيل الذكي

### قراءة مسجلات الجهاز

```bash
# البحث عن بطاقة الشبكة
landauer pci list | grep "8086.*Network"
# → PCI 07:00.0 8086:1533 02:00:00 Network Controller

# عرض تفاصيل الجهاز
landauer pci info 07:00.0

# قراءة معرف البائع/الجهاز من مساحة التهيئة
landauer pci cfg read 07:00.0 0 4
# → 0x15338086

# استعراض الإمكانيات
landauer pci cap list 07:00.0
# → [40] PM, [50] MSI, [70] MSI-X, [A0] PCIe
```

### القراءة والكتابة عبر MMIO

```bash
# تعيين BAR
landauer pci bar map 07:00.0 0
# → BAR0 mapped: handle=1 base=0xDF100000 size=0x100000

# قراءة وكتابة المسجلات
landauer pci bar read 1 0x5400 2
landauer pci bar write 1 0x12114 0x00000000

# تفريغ المحتويات إلى ملف
landauer pci bar dump 1 0 0x200 bar0.bin

# تحرير التعيين
landauer pci bar unmap 1
```

---

## التصميم المعماري

- **تعريف نواة نمط NT** — يعتمد على NT API الخالص (IoCreateDevice + توزيع IRP)، بدون الاعتماد على WDF
- **نمط المزوّد (Provider)** — مزوّد PCI_CFG (HalGetBusDataByOffset) + مزوّد PCI_BAR (MmMapIoSpace)
- **IOCTL واحد** — رأس أوامر بطول 32 بايت + حمولة متغيرة الطول، توزيع ثنائي المستوى (resource_type + operation)
- **بروتوكول ذاتي الوصف** — يحتوي رأس الأمر على magic/version/status، مما يسمح بتطور التعريف وواجهة الأوامر بشكل مستقل

للاطلاع على التفاصيل: [ARCHITECTURE.md](ARCHITECTURE.md)

---

## خريطة التوسع

| الإصدار | المحتوى |
|------|------|
| v1 (الحالي) | وظائف PCIe كاملة: مساحة CFG، BAR MMIO، الإمكانيات |
| v2 | منافذ الإدخال والإخراج، MSR، CPUID |
| v3 | ذاكرة فيزيائية عشوائية، SMBIOS/ACPI |
| v4 | SPI Flash، I2C، GPIO |
| v5 | نقاط توقف عتادية، جدول MSI-X، حقن AER |

---

## اصطلاحات المخرجات

جميع البيانات تُكتب إلى stdout، والأخطاء إلى stderr. التنسيق ثابت وقابل للمعالجة بواسطة grep/awk:

```
# نجاح: exit 0
0xDF100000: 0x00000005

# فشل: exit != 0, stderr
ERR: Failed to read PCI config: ACCESS_DENIED (0xC0000022)
```