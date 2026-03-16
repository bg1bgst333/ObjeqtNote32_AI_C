/*
 * ObjeqtNote - Win32+C++03 テキスト + BMP エディタ
 *
 * .txtファイルを開いた場合:
 *   縦に 文字コードCB / BOM CB / エディット / 改行コードCB を配置し
 *   読み込み・編集・保存ができる。
 *
 * .bmpファイルを開いた場合:
 *   BMPヘッダ16フィールドを縦に表示し、
 *   biBitCountのみコンボで変更可能（1/4/8/24bpp）。
 *   その下にペイント機能付きBMPキャンバスを表示。
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <vector>
#include <string>

// ---- Control / Menu IDs ----------------------------------------
#define IDM_FILE_OPEN  1001
#define IDM_FILE_SAVE  1002

#define IDC_LABEL_ENC  2001
#define IDC_COMBO_ENC  2002
#define IDC_LABEL_BOM  2003
#define IDC_COMBO_BOM  2004
#define IDC_EDIT_TEXT  2005
#define IDC_LABEL_EOL  2006
#define IDC_COMBO_EOL  2007

#define IDC_BMP_BITCOUNT  3001
#define IDC_BMP_CANVAS    3002

// ---- Enumerations ----------------------------------------------
enum Encoding {
    ENC_SJIS = 0, ENC_UTF8, ENC_UTF16LE, ENC_UTF16BE, ENC_JIS, ENC_EUC,
    ENC_COUNT
};
enum BomType {
    BOM_NONE = 0, BOM_UTF8, BOM_UTF16LE, BOM_UTF16BE,
    BOM_COUNT
};
enum EolType {
    EOL_CRLF = 0, EOL_LF, EOL_CR,
    EOL_COUNT
};
enum FileType {
    FT_NONE = 0, FT_TEXT, FT_BMP
};

// ---- Types -----------------------------------------------------
typedef std::vector<unsigned char> ByteVec;

// ---- Globals ---------------------------------------------------
static HINSTANCE g_hInst        = NULL;
static HWND      g_hwnd         = NULL;
static HWND      g_hLabelEnc    = NULL;
static HWND      g_hComboEnc    = NULL;
static HWND      g_hLabelBom    = NULL;
static HWND      g_hComboBom    = NULL;
static HWND      g_hEdit        = NULL;
static HWND      g_hLabelEol    = NULL;
static HWND      g_hComboEol    = NULL;
static HFONT     g_hEditFont    = NULL;
static bool      g_fileOpen     = false;
static bool      g_textCreated  = false;
static WCHAR     g_filePath[MAX_PATH] = { 0 };
static FileType  g_fileType     = FT_NONE;

// BMP コントロール
#define BMP_FIELD_COUNT  16
#define BMP_BITCOUNT_IDX 9
static HWND    g_hBmpLbl[BMP_FIELD_COUNT];
static HWND    g_hBmpVal[BMP_FIELD_COUNT];
static HWND    g_hBmpCanvas  = NULL;
static bool    g_bmpCreated  = false;
static COLORREF g_drawColor  = RGB(0, 0, 0);
static bool    g_drawing     = false;
static ByteVec g_bmpData;

// キャンバス定数
static const int CANVAS_COLOR_BAR_H = 24;
static const int SWATCH_X1 = 64;
static const int SWATCH_X2 = 96;
static const int SWATCH_Y1 = 4;
static const int SWATCH_Y2 = 20;

// ---- 文字列テーブル --------------------------------------------
static const WCHAR* ENC_NAMES[ENC_COUNT] = {
    L"SJIS", L"UTF-8", L"UTF-16LE", L"UTF-16BE", L"JIS", L"EUC"
};
static const WCHAR* BOM_NAMES[BOM_COUNT] = {
    L"\u306a\u3057", L"UTF-8 BOM", L"UTF-16LE BOM", L"UTF-16BE BOM"
};
static const WCHAR* EOL_NAMES[EOL_COUNT] = {
    L"CRLF", L"LF", L"CR"
};
static const WCHAR* BMP_FIELD_NAMES[BMP_FIELD_COUNT] = {
    L"bfType",
    L"bfSize",
    L"bfReserved1",
    L"bfReserved2",
    L"bfOffBits",
    L"biSize",
    L"biWidth",
    L"biHeight",
    L"biPlanes",
    L"biBitCount",
    L"biCompression",
    L"biSizeImage",
    L"biXPelsPerMeter",
    L"biYPelsPerMeter",
    L"biClrUsed",
    L"biClrImportant"
};

static const unsigned char BOM_BYTES_UTF8[]    = { 0xEF, 0xBB, 0xBF };
static const unsigned char BOM_BYTES_UTF16LE[] = { 0xFF, 0xFE };
static const unsigned char BOM_BYTES_UTF16BE[] = { 0xFE, 0xFF };

// ================================================================
// File I/O
// ================================================================
static bool ReadAllBytes(const WCHAR* path, ByteVec& out)
{
    HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(hf, NULL);
    if (sz == INVALID_FILE_SIZE) { CloseHandle(hf); return false; }
    if (sz == 0)                 { CloseHandle(hf); return true;  }
    out.resize(sz);
    DWORD rd = 0;
    BOOL ok = ReadFile(hf, &out[0], sz, &rd, NULL);
    CloseHandle(hf);
    return ok != FALSE && rd == sz;
}

static bool WriteAllBytes(const WCHAR* path, const ByteVec& data)
{
    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return false;
    if (data.empty()) { CloseHandle(hf); return true; }
    DWORD wr = 0;
    BOOL ok = WriteFile(hf, &data[0], (DWORD)data.size(), &wr, NULL);
    CloseHandle(hf);
    return ok != FALSE && wr == (DWORD)data.size();
}

// ================================================================
// BOM detection
// ================================================================
static BomType DetectBOM(const ByteVec& data, size_t& bomSize)
{
    if (data.size() >= 3 &&
        data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
    { bomSize = 3; return BOM_UTF8; }

    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFE)
    { bomSize = 2; return BOM_UTF16LE; }

    if (data.size() >= 2 && data[0] == 0xFE && data[1] == 0xFF)
    { bomSize = 2; return BOM_UTF16BE; }

    bomSize = 0; return BOM_NONE;
}

// ================================================================
// Encoding detection (heuristic, no-BOM)
// ================================================================
static Encoding DetectEncoding(const ByteVec& data, size_t offset)
{
    if (data.size() <= offset) return ENC_UTF8;

    const unsigned char* p   = &data[offset];
    size_t               len = data.size() - offset;

    // ISO-2022-JP: ESC $ B / ESC $ @ / ESC ( B / ESC ( J
    for (size_t i = 0; i + 2 < len; ++i) {
        if (p[i] == 0x1B) {
            if (p[i+1] == '$' && (p[i+2] == 'B' || p[i+2] == '@')) return ENC_JIS;
            if (p[i+1] == '(' && (p[i+2] == 'B' || p[i+2] == 'J')) return ENC_JIS;
        }
    }

    int scoreUtf8 = 0, scoreEuc = 0, scoreSjis = 0;
    for (size_t i = 0; i < len; ) {
        unsigned char c = p[i];
        if (c < 0x80) { ++i; continue; }

        // UTF-8 2-byte
        if ((c & 0xE0) == 0xC0 && i+1 < len && (p[i+1] & 0xC0) == 0x80)
        { ++scoreUtf8; i += 2; continue; }

        // UTF-8 3-byte
        if ((c & 0xF0) == 0xE0 && i+2 < len &&
            (p[i+1] & 0xC0) == 0x80 && (p[i+2] & 0xC0) == 0x80)
        { scoreUtf8 += 2; i += 3; continue; }

        // EUC-JP: 0xA1-0xFE 0xA1-0xFE
        if (c >= 0xA1 && c <= 0xFE && i+1 < len &&
            p[i+1] >= 0xA1 && p[i+1] <= 0xFE)
        { ++scoreEuc; i += 2; continue; }

        // Shift-JIS
        if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && i+1 < len &&
            ((p[i+1] >= 0x40 && p[i+1] <= 0x7E) || (p[i+1] >= 0x80 && p[i+1] <= 0xFC)))
        { ++scoreSjis; i += 2; continue; }

        ++i;
    }

    if (scoreUtf8 > 0 && scoreUtf8 >= scoreEuc && scoreUtf8 >= scoreSjis) return ENC_UTF8;
    if (scoreEuc > scoreSjis) return ENC_EUC;
    if (scoreSjis > 0)        return ENC_SJIS;
    return ENC_UTF8;
}

// ================================================================
// Encoding conversion
// ================================================================
static UINT CodePageOf(Encoding enc)
{
    switch (enc) {
    case ENC_SJIS: return 932;
    case ENC_UTF8: return CP_UTF8;
    case ENC_JIS:  return 50220;
    case ENC_EUC:  return 20932;
    default:       return CP_UTF8;
    }
}

static std::wstring BytesToWide(const ByteVec& data, size_t offset, Encoding enc)
{
    if (data.size() <= offset) return std::wstring();
    const unsigned char* raw = &data[offset];
    size_t len = data.size() - offset;

    if (enc == ENC_UTF16LE) {
        return std::wstring(reinterpret_cast<const wchar_t*>(raw), len / 2);
    }
    if (enc == ENC_UTF16BE) {
        size_t wlen = len / 2;
        std::wstring result(wlen, L'\0');
        for (size_t i = 0; i < wlen; ++i)
            result[i] = (wchar_t)((raw[i*2] << 8) | raw[i*2+1]);
        return result;
    }

    UINT cp = CodePageOf(enc);
    int wlen = MultiByteToWideChar(cp, 0,
                   reinterpret_cast<const char*>(raw), (int)len, NULL, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(cp, 0,
        reinterpret_cast<const char*>(raw), (int)len, &result[0], wlen);
    return result;
}

static ByteVec WideToBytes(const std::wstring& text, Encoding enc)
{
    if (text.empty()) return ByteVec();

    if (enc == ENC_UTF16LE) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&text[0]);
        return ByteVec(p, p + text.size() * 2);
    }
    if (enc == ENC_UTF16BE) {
        ByteVec result(text.size() * 2);
        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t c = text[i];
            result[i*2]   = (unsigned char)((c >> 8) & 0xFF);
            result[i*2+1] = (unsigned char)(c        & 0xFF);
        }
        return result;
    }

    UINT cp  = CodePageOf(enc);
    int  len = WideCharToMultiByte(cp, 0, &text[0], (int)text.size(),
                                   NULL, 0, NULL, NULL);
    if (len <= 0) return ByteVec();
    ByteVec result(len);
    WideCharToMultiByte(cp, 0, &text[0], (int)text.size(),
        reinterpret_cast<char*>(&result[0]), len, NULL, NULL);
    return result;
}

// ================================================================
// EOL handling
// ================================================================
static EolType DetectEol(const std::wstring& text)
{
    int crlf = 0, lf = 0, cr = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i+1 < text.size() && text[i+1] == L'\n') { ++crlf; ++i; }
            else ++cr;
        } else if (text[i] == L'\n') {
            ++lf;
        }
    }
    if (crlf >= lf && crlf >= cr) return EOL_CRLF;
    if (lf >= cr)                 return EOL_LF;
    return EOL_CR;
}

// 任意の改行を \r\n に正規化 (EDIT コントロール用)
static std::wstring ToEditEol(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 4 + 4);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i+1 < text.size() && text[i+1] == L'\n') ++i;
            out += L'\r'; out += L'\n';
        } else if (text[i] == L'\n') {
            out += L'\r'; out += L'\n';
        } else {
            out += text[i];
        }
    }
    return out;
}

// EDIT の \r\n を指定改行コードに変換して保存用テキストを作る
static std::wstring ApplyEol(const std::wstring& text, EolType eol)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i+1 < text.size() && text[i+1] == L'\n') ++i;
            switch (eol) {
            case EOL_CRLF: out += L'\r'; out += L'\n'; break;
            case EOL_LF:   out += L'\n';               break;
            case EOL_CR:   out += L'\r';               break;
            default:                                    break;
            }
        } else if (text[i] == L'\n') {
            switch (eol) {
            case EOL_CRLF: out += L'\r'; out += L'\n'; break;
            case EOL_LF:   out += L'\n';               break;
            case EOL_CR:   out += L'\r';               break;
            default:                                    break;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

// ================================================================
// BMP バイトヘルパー
// ================================================================
static WORD  RdU16(const BYTE* p)
{
    return (WORD)(p[0] | ((WORD)p[1] << 8));
}
static DWORD RdU32(const BYTE* p)
{
    return (DWORD)(p[0] | ((DWORD)p[1]<<8) | ((DWORD)p[2]<<16) | ((DWORD)p[3]<<24));
}
static LONG  RdS32(const BYTE* p)
{
    return (LONG)RdU32(p);
}
static void WrU16(BYTE* p, WORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >> 8) & 0xFF);
}
static void WrU32(BYTE* p, DWORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >>  8) & 0xFF);
    p[2] = (BYTE)((v >> 16) & 0xFF);
    p[3] = (BYTE)((v >> 24) & 0xFF);
}

// ================================================================
// RGB 構造体 + パレット
// ================================================================
struct Rgb { BYTE r, g, b; };

static const Rgb PAL1[2] = { {0,0,0}, {255,255,255} };

static const Rgb PAL4[16] = {
    {0,0,0},       {128,0,0},     {0,128,0},     {128,128,0},
    {0,0,128},     {128,0,128},   {0,128,128},   {192,192,192},
    {128,128,128}, {255,0,0},     {0,255,0},     {255,255,0},
    {0,0,255},     {255,0,255},   {0,255,255},   {255,255,255}
};

static void BuildPal8(Rgb pal[256])
{
    int idx = 0;
    // 6x6x6 カラーキューブ
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b) {
                if (idx < 256) {
                    pal[idx].r = (BYTE)(r * 51);
                    pal[idx].g = (BYTE)(g * 51);
                    pal[idx].b = (BYTE)(b * 51);
                    ++idx;
                }
            }
    // グレースケールで残りを埋める
    for (int i = 0; idx < 256; ++i, ++idx) {
        BYTE v = (i < 40) ? (BYTE)(i * 255 / 39) : 255;
        pal[idx].r = pal[idx].g = pal[idx].b = v;
    }
}

// ================================================================
// BMP 変換
// ================================================================

// BMP バイト列 → 24bit Rgb 配列 (内部表現)
static bool BmpToRgb(const ByteVec& bmp, int& outW, int& outH,
                     std::vector<Rgb>& px)
{
    if (bmp.size() < 54) return false;
    if (bmp[0] != 'B' || bmp[1] != 'M') return false;

    DWORD offBits = RdU32(&bmp[10]);
    int   bmpW    = (int)RdS32(&bmp[18]);
    int   bmpH    = (int)RdS32(&bmp[22]);
    WORD  bpp     = RdU16(&bmp[28]);
    DWORD compr   = RdU32(&bmp[30]);

    if (compr != 0) return false;
    if (bmpW <= 0)  return false;

    bool flipY = (bmpH > 0);
    int  absH  = (bmpH < 0) ? -bmpH : bmpH;
    if (absH == 0) return false;

    // パレット
    DWORD biSize   = RdU32(&bmp[14]);
    int   palStart = (int)(14 + biSize);
    int palColors  = 0;
    if (bpp <= 8) {
        DWORD clrUsed = RdU32(&bmp[46]);
        palColors = (clrUsed > 0) ? (int)clrUsed : (1 << bpp);
    }

    std::vector<Rgb> pal((size_t)palColors);
    for (int i = 0; i < palColors; ++i) {
        int off = palStart + i * 4;
        if ((size_t)(off + 2) < bmp.size()) {
            pal[i].b = bmp[off + 0];
            pal[i].g = bmp[off + 1];
            pal[i].r = bmp[off + 2];
        }
    }

    // 行ストライド
    int rowBytes;
    if      (bpp == 1)  rowBytes = ((bmpW + 31) / 32) * 4;
    else if (bpp == 4)  rowBytes = ((bmpW * 4 + 31) / 32) * 4;
    else if (bpp == 8)  rowBytes = ((bmpW * 8 + 31) / 32) * 4;
    else if (bpp == 24) rowBytes = ((bmpW * 24 + 31) / 32) * 4;
    else return false;

    if (offBits + (size_t)rowBytes * (size_t)absH > bmp.size()) return false;

    outW = bmpW;
    outH = absH;
    px.resize((size_t)bmpW * (size_t)absH);

    for (int y = 0; y < absH; ++y) {
        int srcRow = flipY ? (absH - 1 - y) : y;
        const BYTE* row = &bmp[offBits + (size_t)srcRow * (size_t)rowBytes];
        for (int x = 0; x < bmpW; ++x) {
            Rgb c;
            c.r = c.g = c.b = 0;
            if (bpp == 1) {
                int bit = (row[x/8] >> (7 - x%8)) & 1;
                c = (bit < palColors) ? pal[bit] : PAL1[bit & 1];
            } else if (bpp == 4) {
                int nibble = (x % 2 == 0) ? (row[x/2] >> 4) : (row[x/2] & 0x0F);
                c = (nibble < palColors) ? pal[nibble] : PAL4[nibble & 0xF];
            } else if (bpp == 8) {
                int idx = row[x];
                if (idx < palColors) c = pal[idx];
            } else {
                c.b = row[x*3 + 0];
                c.g = row[x*3 + 1];
                c.r = row[x*3 + 2];
            }
            px[(size_t)y * (size_t)bmpW + (size_t)x] = c;
        }
    }
    return true;
}

// 24bit Rgb 配列 → BMP ByteVec
static ByteVec RgbToBmp(const std::vector<Rgb>& px, int w, int h, int bpp)
{
    int palColors = (bpp <= 8) ? (1 << bpp) : 0;
    int palBytes  = palColors * 4;

    int rowBytes;
    if      (bpp == 1)  rowBytes = ((w + 31) / 32) * 4;
    else if (bpp == 4)  rowBytes = ((w * 4 + 31) / 32) * 4;
    else if (bpp == 8)  rowBytes = ((w * 8 + 31) / 32) * 4;
    else                rowBytes = ((w * 24 + 31) / 32) * 4;

    int pixDataSize = rowBytes * h;
    int offBits     = 14 + 40 + palBytes;
    int fileSize    = offBits + pixDataSize;

    ByteVec out((size_t)fileSize, 0);

    // BITMAPFILEHEADER
    out[0] = 'B'; out[1] = 'M';
    WrU32(&out[2],  (DWORD)fileSize);
    WrU32(&out[6],  0);
    WrU32(&out[10], (DWORD)offBits);

    // BITMAPINFOHEADER
    WrU32(&out[14], 40);
    WrU32(&out[18], (DWORD)w);
    WrU32(&out[22], (DWORD)h);
    WrU16(&out[26], 1);
    WrU16(&out[28], (WORD)bpp);
    WrU32(&out[30], 0);
    WrU32(&out[34], (DWORD)pixDataSize);
    WrU32(&out[38], 0);
    WrU32(&out[42], 0);
    WrU32(&out[46], (DWORD)palColors);
    WrU32(&out[50], 0);

    // パレット書き込み
    if (bpp == 1) {
        // Entry 0: Black
        out[54+0]=0;   out[54+1]=0;   out[54+2]=0;   out[54+3]=0;
        // Entry 1: White
        out[54+4]=255; out[54+5]=255; out[54+6]=255; out[54+7]=0;
    } else if (bpp == 4) {
        for (int i = 0; i < 16; ++i) {
            out[54 + i*4 + 0] = PAL4[i].b;
            out[54 + i*4 + 1] = PAL4[i].g;
            out[54 + i*4 + 2] = PAL4[i].r;
            out[54 + i*4 + 3] = 0;
        }
    } else if (bpp == 8) {
        Rgb pal[256];
        BuildPal8(pal);
        for (int i = 0; i < 256; ++i) {
            out[54 + i*4 + 0] = pal[i].b;
            out[54 + i*4 + 1] = pal[i].g;
            out[54 + i*4 + 2] = pal[i].r;
            out[54 + i*4 + 3] = 0;
        }
    }

    // ピクセルデータ (8bpp 用パレットをここで生成)
    Rgb pal8[256];
    if (bpp == 8) BuildPal8(pal8);

    for (int y = 0; y < h; ++y) {
        int dstRow = h - 1 - y; // bottom-up
        BYTE* row = &out[(size_t)offBits + (size_t)dstRow * (size_t)rowBytes];

        for (int x = 0; x < w; ++x) {
            const Rgb& c = px[(size_t)y * (size_t)w + (size_t)x];
            if (bpp == 24) {
                row[x*3 + 0] = c.b;
                row[x*3 + 1] = c.g;
                row[x*3 + 2] = c.r;
            } else if (bpp == 1) {
                int lum = (c.r * 299 + c.g * 587 + c.b * 114) / 1000;
                int bit = (lum >= 128) ? 1 : 0;
                if (bit) row[x/8] |= (BYTE)(1 << (7 - x%8));
                else     row[x/8] &= (BYTE)(~(1 << (7 - x%8)));
            } else if (bpp == 4) {
                int bestIdx = 0, bestDist = 0x7FFFFFFF;
                for (int i = 0; i < 16; ++i) {
                    int dr = c.r - PAL4[i].r;
                    int dg = c.g - PAL4[i].g;
                    int db = c.b - PAL4[i].b;
                    int d = dr*dr + dg*dg + db*db;
                    if (d < bestDist) { bestDist = d; bestIdx = i; }
                }
                if (x % 2 == 0)
                    row[x/2] = (BYTE)((row[x/2] & 0x0F) | (bestIdx << 4));
                else
                    row[x/2] = (BYTE)((row[x/2] & 0xF0) | (bestIdx & 0x0F));
            } else { // 8bpp
                int bestIdx = 0, bestDist = 0x7FFFFFFF;
                for (int i = 0; i < 256; ++i) {
                    int dr = c.r - pal8[i].r;
                    int dg = c.g - pal8[i].g;
                    int db = c.b - pal8[i].b;
                    int d = dr*dr + dg*dg + db*db;
                    if (d < bestDist) { bestDist = d; bestIdx = i; }
                }
                row[x] = (BYTE)bestIdx;
            }
        }
    }
    return out;
}

// BMP を新しい bpp に変換
static ByteVec ConvertBmpBpp(const ByteVec& src, int newBpp)
{
    int w, h;
    std::vector<Rgb> px;
    if (!BmpToRgb(src, w, h, px)) return src;
    return RgbToBmp(px, w, h, newBpp);
}

// ================================================================
// BMP ピクセル操作
// ================================================================
static COLORREF BmpGetPixel(const ByteVec& bmp, int bmpX, int bmpY)
{
    if (bmp.size() < 54) return RGB(0,0,0);

    DWORD offBits = RdU32(&bmp[10]);
    int   bmpW    = (int)RdS32(&bmp[18]);
    int   bmpH    = (int)RdS32(&bmp[22]);
    WORD  bpp     = RdU16(&bmp[28]);
    int   absH    = (bmpH < 0) ? -bmpH : bmpH;

    if (bmpX < 0 || bmpY < 0 || bmpX >= bmpW || bmpY >= absH) return RGB(0,0,0);

    int rowBytes;
    if      (bpp == 1)  rowBytes = ((bmpW + 31) / 32) * 4;
    else if (bpp == 4)  rowBytes = ((bmpW * 4 + 31) / 32) * 4;
    else if (bpp == 8)  rowBytes = ((bmpW * 8 + 31) / 32) * 4;
    else if (bpp == 24) rowBytes = ((bmpW * 24 + 31) / 32) * 4;
    else return RGB(0,0,0);

    DWORD biSize  = RdU32(&bmp[14]);
    int   palOff  = (int)(14 + biSize);
    int   fileRow = (bmpH > 0) ? (absH - 1 - bmpY) : bmpY;

    if (offBits + (size_t)(fileRow + 1) * (size_t)rowBytes > bmp.size())
        return RGB(0,0,0);

    const BYTE* row = &bmp[offBits + (size_t)fileRow * (size_t)rowBytes];
    BYTE r = 0, g = 0, b = 0;

    if (bpp == 24) {
        b = row[bmpX*3 + 0];
        g = row[bmpX*3 + 1];
        r = row[bmpX*3 + 2];
    } else if (bpp == 1) {
        int bit = (row[bmpX/8] >> (7 - bmpX%8)) & 1;
        r = g = b = bit ? 255 : 0;
    } else if (bpp == 4) {
        int nibble = (bmpX % 2 == 0) ? (row[bmpX/2] >> 4) : (row[bmpX/2] & 0x0F);
        int off = palOff + nibble * 4;
        if ((size_t)(off + 2) < bmp.size()) {
            b = bmp[off+0]; g = bmp[off+1]; r = bmp[off+2];
        }
    } else { // 8bpp
        int idx = row[bmpX];
        int off = palOff + idx * 4;
        if ((size_t)(off + 2) < bmp.size()) {
            b = bmp[off+0]; g = bmp[off+1]; r = bmp[off+2];
        }
    }
    return RGB(r, g, b);
}

static void BmpSetPixel(ByteVec& bmp, int bmpX, int bmpY, COLORREF color)
{
    if (bmp.size() < 54) return;

    DWORD offBits = RdU32(&bmp[10]);
    int   bmpW    = (int)RdS32(&bmp[18]);
    int   bmpH    = (int)RdS32(&bmp[22]);
    WORD  bpp     = RdU16(&bmp[28]);
    int   absH    = (bmpH < 0) ? -bmpH : bmpH;

    if (bmpX < 0 || bmpY < 0 || bmpX >= bmpW || bmpY >= absH) return;

    int rowBytes;
    if      (bpp == 1)  rowBytes = ((bmpW + 31) / 32) * 4;
    else if (bpp == 4)  rowBytes = ((bmpW * 4 + 31) / 32) * 4;
    else if (bpp == 8)  rowBytes = ((bmpW * 8 + 31) / 32) * 4;
    else if (bpp == 24) rowBytes = ((bmpW * 24 + 31) / 32) * 4;
    else return;

    DWORD biSize  = RdU32(&bmp[14]);
    int   palOff  = (int)(14 + biSize);
    int   fileRow = (bmpH > 0) ? (absH - 1 - bmpY) : bmpY;

    if (offBits + (size_t)(fileRow + 1) * (size_t)rowBytes > bmp.size()) return;

    BYTE* row = &bmp[offBits + (size_t)fileRow * (size_t)rowBytes];
    BYTE  cr  = GetRValue(color);
    BYTE  cg  = GetGValue(color);
    BYTE  cb  = GetBValue(color);

    if (bpp == 24) {
        row[bmpX*3 + 0] = cb;
        row[bmpX*3 + 1] = cg;
        row[bmpX*3 + 2] = cr;
    } else if (bpp == 1) {
        int lum = (cr * 299 + cg * 587 + cb * 114) / 1000;
        int bit = (lum >= 128) ? 1 : 0;
        if (bit) row[bmpX/8] |= (BYTE)(1 << (7 - bmpX%8));
        else     row[bmpX/8] &= (BYTE)(~(1 << (7 - bmpX%8)));
    } else if (bpp == 4) {
        int bestIdx = 0, bestDist = 0x7FFFFFFF;
        for (int i = 0; i < 16; ++i) {
            int dr = cr - PAL4[i].r;
            int dg = cg - PAL4[i].g;
            int db = cb - PAL4[i].b;
            int d  = dr*dr + dg*dg + db*db;
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        if (bmpX % 2 == 0)
            row[bmpX/2] = (BYTE)((row[bmpX/2] & 0x0F) | (bestIdx << 4));
        else
            row[bmpX/2] = (BYTE)((row[bmpX/2] & 0xF0) | (bestIdx & 0x0F));
    } else { // 8bpp: パレットから最近傍色を探す
        int palColors = 256;
        DWORD clrUsed = RdU32(&bmp[46]);
        if (clrUsed > 0 && clrUsed < 256) palColors = (int)clrUsed;

        int bestIdx = 0, bestDist = 0x7FFFFFFF;
        for (int i = 0; i < palColors; ++i) {
            int off = palOff + i * 4;
            if ((size_t)(off + 2) >= bmp.size()) break;
            int dr = cr - bmp[off+2];
            int dg = cg - bmp[off+1];
            int db = cb - bmp[off+0];
            int d  = dr*dr + dg*dg + db*db;
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        row[bmpX] = (BYTE)bestIdx;
    }
}

// ================================================================
// BMP キャンバス ウィンドウプロシージャ
// ================================================================
static void CanvasDoPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    // カラーバー背景
    HBRUSH hBarBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    RECT barRect = { 0, 0, W, CANVAS_COLOR_BAR_H };
    FillRect(hdc, &barRect, hBarBr);
    DeleteObject(hBarBr);

    // "描画色:" ラベル
    SetBkMode(hdc, TRANSPARENT);
    HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hOld = (HFONT)SelectObject(hdc, hGui);
    TextOutW(hdc, 4, 5, L"\u63cf\u753b\u8272:", 4);
    SelectObject(hdc, hOld);

    // カラースウォッチ
    RECT swatchRect = { SWATCH_X1, SWATCH_Y1, SWATCH_X2, SWATCH_Y2 };
    HBRUSH hSw = CreateSolidBrush(g_drawColor);
    FillRect(hdc, &swatchRect, hSw);
    DeleteObject(hSw);
    FrameRect(hdc, &swatchRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // BMP 描画
    int drawAreaH = H - CANVAS_COLOR_BAR_H;
    if (drawAreaH > 0 && !g_bmpData.empty() && g_bmpData.size() >= 54) {
        DWORD offBits = RdU32(&g_bmpData[10]);
        int   bmpW    = (int)RdS32(&g_bmpData[18]);
        int   bmpH    = (int)RdS32(&g_bmpData[22]);
        WORD  bpp     = RdU16(&g_bmpData[28]);
        int   absH    = (bmpH < 0) ? -bmpH : bmpH;

        if (bmpW > 0 && absH > 0 && offBits < (DWORD)g_bmpData.size()) {
            // BITMAPINFO をアライン済みバッファに構築
            int palColors = (bpp <= 8) ? (1 << bpp) : 0;
            size_t bmiSize = sizeof(BITMAPINFOHEADER)
                           + (size_t)palColors * sizeof(RGBQUAD);
            std::vector<BYTE> bmiBuf(bmiSize, 0);
            BITMAPINFO* pbi = (BITMAPINFO*)(&bmiBuf[0]);

            DWORD biSize = RdU32(&g_bmpData[14]);
            int   palOff = (int)(14 + biSize);

            pbi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
            pbi->bmiHeader.biWidth         = bmpW;
            pbi->bmiHeader.biHeight        = bmpH;
            pbi->bmiHeader.biPlanes        = 1;
            pbi->bmiHeader.biBitCount      = bpp;
            pbi->bmiHeader.biCompression   = BI_RGB;
            pbi->bmiHeader.biSizeImage     = 0;
            pbi->bmiHeader.biXPelsPerMeter = 0;
            pbi->bmiHeader.biYPelsPerMeter = 0;
            pbi->bmiHeader.biClrUsed       = (DWORD)palColors;
            pbi->bmiHeader.biClrImportant  = 0;

            for (int i = 0; i < palColors; ++i) {
                int off = palOff + i * 4;
                if ((size_t)(off + 3) < g_bmpData.size()) {
                    pbi->bmiColors[i].rgbBlue     = g_bmpData[off+0];
                    pbi->bmiColors[i].rgbGreen    = g_bmpData[off+1];
                    pbi->bmiColors[i].rgbRed      = g_bmpData[off+2];
                    pbi->bmiColors[i].rgbReserved = 0;
                }
            }

            const void* pBits = &g_bmpData[offBits];
            StretchDIBits(hdc,
                0, CANVAS_COLOR_BAR_H, W, drawAreaH,
                0, 0, bmpW, absH,
                pBits, pbi,
                DIB_RGB_COLORS, SRCCOPY);
        }
    }

    EndPaint(hwnd, &ps);
}

static void CanvasDrawAt(HWND hwnd, int mx, int my)
{
    if (g_bmpData.size() < 54) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W         = rc.right;
    int H         = rc.bottom;
    int drawAreaH = H - CANVAS_COLOR_BAR_H;
    int drawY     = my - CANVAS_COLOR_BAR_H;

    if (W <= 0 || drawAreaH <= 0 || drawY < 0) return;

    int bmpW = (int)RdS32(&g_bmpData[18]);
    int bmpH = (int)RdS32(&g_bmpData[22]);
    int absH = (bmpH < 0) ? -bmpH : bmpH;
    if (bmpW <= 0 || absH <= 0) return;

    int bmpX = mx   * bmpW / W;
    int bmpY = drawY * absH / drawAreaH;

    BmpSetPixel(g_bmpData, bmpX, bmpY, g_drawColor);
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK BmpCanvasProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:
        CanvasDoPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN: {
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        // スウォッチクリック判定
        if (mx >= SWATCH_X1 && mx < SWATCH_X2 &&
            my >= SWATCH_Y1 && my < SWATCH_Y2) {
            static COLORREF custColors[16];
            CHOOSECOLORW cc;
            ZeroMemory(&cc, sizeof(cc));
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.rgbResult    = g_drawColor;
            cc.lpCustColors = custColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_drawColor = cc.rgbResult;
                RECT barRect = { 0, 0, 200, CANVAS_COLOR_BAR_H };
                InvalidateRect(hwnd, &barRect, FALSE);
            }
        } else if (my >= CANVAS_COLOR_BAR_H) {
            g_drawing = true;
            SetCapture(hwnd);
            CanvasDrawAt(hwnd, mx, my);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_drawing && (wParam & MK_LBUTTON)) {
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);
            if (my >= CANVAS_COLOR_BAR_H)
                CanvasDrawAt(hwnd, mx, my);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_drawing) {
            g_drawing = false;
            ReleaseCapture();
        }
        return 0;

    case WM_RBUTTONDOWN: {
        int mx = (int)(short)LOWORD(lParam);
        int my = (int)(short)HIWORD(lParam);
        if (my >= CANVAS_COLOR_BAR_H && !g_bmpData.empty()) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int W         = rc.right;
            int H         = rc.bottom;
            int drawAreaH = H - CANVAS_COLOR_BAR_H;
            int drawY     = my - CANVAS_COLOR_BAR_H;
            if (W > 0 && drawAreaH > 0 && g_bmpData.size() >= 54) {
                int bmpW = (int)RdS32(&g_bmpData[18]);
                int bmpH = (int)RdS32(&g_bmpData[22]);
                int absH = (bmpH < 0) ? -bmpH : bmpH;
                if (bmpW > 0 && absH > 0) {
                    int bmpX = mx   * bmpW / W;
                    int bmpY = drawY * absH / drawAreaH;
                    g_drawColor = BmpGetPixel(g_bmpData, bmpX, bmpY);
                    RECT barRect = { 0, 0, 200, CANVAS_COLOR_BAR_H };
                    InvalidateRect(hwnd, &barRect, FALSE);
                }
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ================================================================
// テキストエディタ コントロール作成 / レイアウト / 表示
// ================================================================
static void CreateEditorControls(HWND hwnd)
{
    HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    g_hLabelEnc = CreateWindowW(L"STATIC", L"\u6587\u5b57\u30b3\u30fc\u30c9:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_ENC, g_hInst, NULL);
    g_hLabelBom = CreateWindowW(L"STATIC", L"BOM:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_BOM, g_hInst, NULL);
    g_hLabelEol = CreateWindowW(L"STATIC", L"\u6539\u884c\u30b3\u30fc\u30c9:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_EOL, g_hInst, NULL);

    g_hComboEnc = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_ENC, g_hInst, NULL);
    g_hComboBom = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_BOM, g_hInst, NULL);
    g_hComboEol = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_EOL, g_hInst, NULL);

    g_hEdit = CreateWindowW(L"EDIT", NULL,
        WS_CHILD | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
        0,0,1,1, hwnd, (HMENU)IDC_EDIT_TEXT, g_hInst, NULL);

    for (int i = 0; i < ENC_COUNT; ++i)
        SendMessageW(g_hComboEnc, CB_ADDSTRING, 0, (LPARAM)ENC_NAMES[i]);
    for (int i = 0; i < BOM_COUNT; ++i)
        SendMessageW(g_hComboBom, CB_ADDSTRING, 0, (LPARAM)BOM_NAMES[i]);
    for (int i = 0; i < EOL_COUNT; ++i)
        SendMessageW(g_hComboEol, CB_ADDSTRING, 0, (LPARAM)EOL_NAMES[i]);

    SendMessageW(g_hLabelEnc, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hLabelBom, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hLabelEol, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboEnc, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboBom, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboEol, WM_SETFONT, (WPARAM)hGui, FALSE);

    g_hEditFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"MS Gothic");
    SendMessageW(g_hEdit, WM_SETFONT,
        (WPARAM)(g_hEditFont ? g_hEditFont : hGui), FALSE);

    SendMessageW(g_hEdit, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFF, 0);
}

static void LayoutEditorControls(HWND hwnd)
{
    if (!g_textCreated) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    const int pad    = 4;
    const int rowH   = 24;
    const int labelW = 90;
    const int comboW = 180;
    const int dropH  = 160;

    int y1  = pad;
    int y2  = y1 + rowH + pad;
    int yE  = y2 + rowH + pad;
    int y4  = H  - pad - rowH;
    int yEb = y4 - pad;
    if (yEb < yE) yEb = yE;

    int x0 = pad;
    int x1 = x0 + labelW + pad;

    MoveWindow(g_hLabelEnc, x0, y1 + 4, labelW, rowH - 4, TRUE);
    MoveWindow(g_hComboEnc, x1, y1,     comboW, rowH + dropH, TRUE);
    MoveWindow(g_hLabelBom, x0, y2 + 4, labelW, rowH - 4, TRUE);
    MoveWindow(g_hComboBom, x1, y2,     comboW, rowH + dropH, TRUE);
    MoveWindow(g_hEdit,     0,  yE,     W,      yEb - yE,    TRUE);
    MoveWindow(g_hLabelEol, x0, y4 + 4, labelW, rowH - 4, TRUE);
    MoveWindow(g_hComboEol, x1, y4,     comboW, rowH + dropH, TRUE);
}

static void ShowEditorControls(bool show)
{
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hLabelEnc, cmd);
    ShowWindow(g_hComboEnc, cmd);
    ShowWindow(g_hLabelBom, cmd);
    ShowWindow(g_hComboBom, cmd);
    ShowWindow(g_hEdit,     cmd);
    ShowWindow(g_hLabelEol, cmd);
    ShowWindow(g_hComboEol, cmd);
}

// ================================================================
// BMP コントロール作成 / レイアウト / 表示 / 更新
// ================================================================
static void CreateBmpControls(HWND hwnd)
{
    HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    for (int i = 0; i < BMP_FIELD_COUNT; ++i) {
        g_hBmpLbl[i] = CreateWindowW(L"STATIC", BMP_FIELD_NAMES[i],
            WS_CHILD | SS_RIGHT,
            0, 0, 1, 1, hwnd, NULL, g_hInst, NULL);
        SendMessageW(g_hBmpLbl[i], WM_SETFONT, (WPARAM)hGui, FALSE);

        if (i == BMP_BITCOUNT_IDX) {
            g_hBmpVal[i] = CreateWindowW(L"COMBOBOX", NULL,
                WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 1, 1, hwnd,
                (HMENU)IDC_BMP_BITCOUNT, g_hInst, NULL);
            SendMessageW(g_hBmpVal[i], CB_ADDSTRING, 0, (LPARAM)L"1");
            SendMessageW(g_hBmpVal[i], CB_ADDSTRING, 0, (LPARAM)L"4");
            SendMessageW(g_hBmpVal[i], CB_ADDSTRING, 0, (LPARAM)L"8");
            SendMessageW(g_hBmpVal[i], CB_ADDSTRING, 0, (LPARAM)L"24");
        } else {
            g_hBmpVal[i] = CreateWindowW(L"STATIC", L"",
                WS_CHILD | SS_LEFT,
                0, 0, 1, 1, hwnd, NULL, g_hInst, NULL);
        }
        SendMessageW(g_hBmpVal[i], WM_SETFONT, (WPARAM)hGui, FALSE);
    }

    g_hBmpCanvas = CreateWindowW(L"ObjeqtNoteBmpCanvas", NULL,
        WS_CHILD | WS_BORDER,
        0, 0, 1, 1, hwnd,
        (HMENU)IDC_BMP_CANVAS, g_hInst, NULL);
}

static void LayoutBmpControls(HWND hwnd)
{
    if (!g_bmpCreated) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    const int pad   = 4;
    const int rowH  = 20;
    const int gap   = 2;
    const int lblW  = 120;
    const int valW  = 180;
    const int dropH = 100;
    const int step  = rowH + gap;

    int x0 = pad;
    int x1 = x0 + lblW + pad;

    for (int i = 0; i < BMP_FIELD_COUNT; ++i) {
        int y = pad + i * step;
        MoveWindow(g_hBmpLbl[i], x0, y + 2, lblW, rowH, TRUE);
        if (i == BMP_BITCOUNT_IDX)
            MoveWindow(g_hBmpVal[i], x1, y, valW, rowH + dropH, TRUE);
        else
            MoveWindow(g_hBmpVal[i], x1, y + 2, valW, rowH, TRUE);
    }

    int canvasTop = pad + BMP_FIELD_COUNT * step;
    int canvasH   = H - canvasTop - pad;
    if (canvasH < 50) canvasH = 50;
    MoveWindow(g_hBmpCanvas, 0, canvasTop, W, canvasH, TRUE);
}

static void ShowBmpControls(bool show)
{
    int cmd = show ? SW_SHOW : SW_HIDE;
    for (int i = 0; i < BMP_FIELD_COUNT; ++i) {
        ShowWindow(g_hBmpLbl[i], cmd);
        ShowWindow(g_hBmpVal[i], cmd);
    }
    if (g_hBmpCanvas) ShowWindow(g_hBmpCanvas, cmd);
}

static void UpdateBmpHeaderDisplay()
{
    if (g_bmpData.size() < 54) return;

    WCHAR buf[64];

    // bfType
    wsprintfW(buf, L"%c%c (0x%04X)",
        (WCHAR)g_bmpData[0], (WCHAR)g_bmpData[1], RdU16(&g_bmpData[0]));
    SetWindowTextW(g_hBmpVal[0], buf);

    // bfSize
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[2]));
    SetWindowTextW(g_hBmpVal[1], buf);

    // bfReserved1
    wsprintfW(buf, L"%u", (UINT)RdU16(&g_bmpData[6]));
    SetWindowTextW(g_hBmpVal[2], buf);

    // bfReserved2
    wsprintfW(buf, L"%u", (UINT)RdU16(&g_bmpData[8]));
    SetWindowTextW(g_hBmpVal[3], buf);

    // bfOffBits
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[10]));
    SetWindowTextW(g_hBmpVal[4], buf);

    // biSize
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[14]));
    SetWindowTextW(g_hBmpVal[5], buf);

    // biWidth
    wsprintfW(buf, L"%d", RdS32(&g_bmpData[18]));
    SetWindowTextW(g_hBmpVal[6], buf);

    // biHeight
    wsprintfW(buf, L"%d", RdS32(&g_bmpData[22]));
    SetWindowTextW(g_hBmpVal[7], buf);

    // biPlanes
    wsprintfW(buf, L"%u", (UINT)RdU16(&g_bmpData[26]));
    SetWindowTextW(g_hBmpVal[8], buf);

    // biBitCount (コンボボックス)
    WORD bpp = RdU16(&g_bmpData[28]);
    int sel;
    if      (bpp == 1)  sel = 0;
    else if (bpp == 4)  sel = 1;
    else if (bpp == 8)  sel = 2;
    else                sel = 3;
    SendMessageW(g_hBmpVal[BMP_BITCOUNT_IDX], CB_SETCURSEL, (WPARAM)sel, 0);

    // biCompression
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[30]));
    SetWindowTextW(g_hBmpVal[10], buf);

    // biSizeImage
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[34]));
    SetWindowTextW(g_hBmpVal[11], buf);

    // biXPelsPerMeter
    wsprintfW(buf, L"%d", RdS32(&g_bmpData[38]));
    SetWindowTextW(g_hBmpVal[12], buf);

    // biYPelsPerMeter
    wsprintfW(buf, L"%d", RdS32(&g_bmpData[42]));
    SetWindowTextW(g_hBmpVal[13], buf);

    // biClrUsed
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[46]));
    SetWindowTextW(g_hBmpVal[14], buf);

    // biClrImportant
    wsprintfW(buf, L"%u", RdU32(&g_bmpData[50]));
    SetWindowTextW(g_hBmpVal[15], buf);
}

// ================================================================
// テキストファイル 開く / 保存
// ================================================================
static void OpenTextFile(HWND hwnd, const WCHAR* path)
{
    ByteVec data;
    if (!ReadAllBytes(path, data)) {
        MessageBoxW(hwnd, L"\u30d5\u30a1\u30a4\u30eb\u3092\u958b\u3051\u307e\u305b\u3093\u3067\u3057\u305f\u3002",
                    L"\u30a8\u30e9\u30fc", MB_OK | MB_ICONERROR);
        return;
    }

    size_t  bomSize = 0;
    BomType bom     = DetectBOM(data, bomSize);

    Encoding enc;
    switch (bom) {
    case BOM_UTF8:    enc = ENC_UTF8;    break;
    case BOM_UTF16LE: enc = ENC_UTF16LE; break;
    case BOM_UTF16BE: enc = ENC_UTF16BE; break;
    default:          enc = DetectEncoding(data, 0); break;
    }

    std::wstring text     = BytesToWide(data, bomSize, enc);
    EolType      eol      = DetectEol(text);
    std::wstring editText = ToEditEol(text);

    SendMessageW(g_hComboEnc, CB_SETCURSEL, (WPARAM)enc, 0);
    SendMessageW(g_hComboBom, CB_SETCURSEL, (WPARAM)bom, 0);
    SendMessageW(g_hComboEol, CB_SETCURSEL, (WPARAM)eol, 0);

    SetWindowTextW(g_hEdit, editText.c_str());

    WCHAR title[MAX_PATH + 32];
    wsprintfW(title, L"ObjeqtNote - %s", path);
    SetWindowTextW(hwnd, title);
}

static void SaveTextFile(HWND hwnd)
{
    if (!g_fileOpen || g_fileType != FT_TEXT) return;

    int encIdx = (int)SendMessageW(g_hComboEnc, CB_GETCURSEL, 0, 0);
    int bomIdx = (int)SendMessageW(g_hComboBom, CB_GETCURSEL, 0, 0);
    int eolIdx = (int)SendMessageW(g_hComboEol, CB_GETCURSEL, 0, 0);

    if (encIdx < 0 || encIdx >= ENC_COUNT) encIdx = 0;
    if (bomIdx < 0 || bomIdx >= BOM_COUNT) bomIdx = 0;
    if (eolIdx < 0 || eolIdx >= EOL_COUNT) eolIdx = 0;

    Encoding enc = (Encoding)encIdx;
    BomType  bom = (BomType)bomIdx;
    EolType  eol = (EolType)eolIdx;

    int len = GetWindowTextLengthW(g_hEdit);
    std::wstring editText(len + 1, L'\0');
    if (len > 0) GetWindowTextW(g_hEdit, &editText[0], len + 1);
    editText.resize(len);

    std::wstring saveText = ApplyEol(editText, eol);
    ByteVec      body     = WideToBytes(saveText, enc);

    ByteVec out;
    switch (bom) {
    case BOM_UTF8:
        out.insert(out.end(), BOM_BYTES_UTF8,    BOM_BYTES_UTF8    + 3); break;
    case BOM_UTF16LE:
        out.insert(out.end(), BOM_BYTES_UTF16LE, BOM_BYTES_UTF16LE + 2); break;
    case BOM_UTF16BE:
        out.insert(out.end(), BOM_BYTES_UTF16BE, BOM_BYTES_UTF16BE + 2); break;
    default: break;
    }
    out.insert(out.end(), body.begin(), body.end());

    if (!WriteAllBytes(g_filePath, out)) {
        MessageBoxW(hwnd, L"\u30d5\u30a1\u30a4\u30eb\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002",
                    L"\u30a8\u30e9\u30fc", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(hwnd, L"\u4fdd\u5b58\u3057\u307e\u3057\u305f\u3002", L"ObjeqtNote", MB_OK);
}

// ================================================================
// BMP ファイル 開く
// ================================================================
static void OpenBmpFile(HWND hwnd, const WCHAR* path)
{
    g_bmpData.clear();
    if (!ReadAllBytes(path, g_bmpData)) {
        MessageBoxW(hwnd, L"\u30d5\u30a1\u30a4\u30eb\u3092\u958b\u3051\u307e\u305b\u3093\u3067\u3057\u305f\u3002",
                    L"\u30a8\u30e9\u30fc", MB_OK | MB_ICONERROR);
        return;
    }

    if (g_bmpData.size() < 54 ||
        g_bmpData[0] != 'B' || g_bmpData[1] != 'M') {
        MessageBoxW(hwnd,
            L"\u6709\u52b9\u306a BMP \u30d5\u30a1\u30a4\u30eb\u3067\u306f\u3042\u308a\u307e\u305b\u3093\u3002",
            L"\u30a8\u30e9\u30fc", MB_OK | MB_ICONERROR);
        g_bmpData.clear();
        return;
    }

    UpdateBmpHeaderDisplay();
    if (g_hBmpCanvas) InvalidateRect(g_hBmpCanvas, NULL, TRUE);

    WCHAR title[MAX_PATH + 32];
    wsprintfW(title, L"ObjeqtNote - %s", path);
    SetWindowTextW(hwnd, title);
}

// ================================================================
// 統合保存
// ================================================================
static void SaveCurrentFile(HWND hwnd)
{
    if (!g_fileOpen) return;

    if (g_fileType == FT_TEXT) {
        SaveTextFile(hwnd);
    } else if (g_fileType == FT_BMP) {
        if (!WriteAllBytes(g_filePath, g_bmpData)) {
            MessageBoxW(hwnd,
                L"\u30d5\u30a1\u30a4\u30eb\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002",
                L"\u30a8\u30e9\u30fc", MB_OK | MB_ICONERROR);
        } else {
            MessageBoxW(hwnd, L"\u4fdd\u5b58\u3057\u307e\u3057\u305f\u3002", L"ObjeqtNote", MB_OK);
        }
    }
}

// ================================================================
// ファイルを開く (テキスト / BMP 判定)
// ================================================================
static void DoFileOpen(HWND hwnd)
{
    WCHAR path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter =
        L"\u30c6\u30ad\u30b9\u30c8\u30d5\u30a1\u30a4\u30eb (*.txt)\0*.txt\0"
        L"BMP\u30d5\u30a1\u30a4\u30eb (*.bmp)\0*.bmp\0"
        L"\u3059\u3079\u3066\u306e\u30d5\u30a1\u30a4\u30eb (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";

    if (!GetOpenFileNameW(&ofn)) return;

    // 拡張子で判定
    int  len   = lstrlenW(path);
    bool isBmp = (len >= 4
        && path[len-4] == L'.'
        && (path[len-3] == L'b' || path[len-3] == L'B')
        && (path[len-2] == L'm' || path[len-2] == L'M')
        && (path[len-1] == L'p' || path[len-1] == L'P'));

    FileType newType = isBmp ? FT_BMP : FT_TEXT;

    if (newType != g_fileType) {
        // 現モードのコントロールを隠す
        if (g_fileType == FT_TEXT) ShowEditorControls(false);
        if (g_fileType == FT_BMP)  ShowBmpControls(false);

        // 必要に応じてコントロールを生成
        if (newType == FT_TEXT && !g_textCreated) {
            CreateEditorControls(hwnd);
            g_textCreated = true;
        }
        if (newType == FT_BMP && !g_bmpCreated) {
            CreateBmpControls(hwnd);
            g_bmpCreated = true;
        }

        g_fileType = newType;

        // 新モードのコントロールを表示
        if (newType == FT_TEXT) {
            ShowEditorControls(true);
            LayoutEditorControls(hwnd);
        }
        if (newType == FT_BMP) {
            ShowBmpControls(true);
            LayoutBmpControls(hwnd);
        }
    } else if (newType == FT_TEXT && !g_textCreated) {
        // 初回テキスト
        CreateEditorControls(hwnd);
        g_textCreated = true;
        g_fileType = FT_TEXT;
        ShowEditorControls(true);
        LayoutEditorControls(hwnd);
    } else if (newType == FT_BMP && !g_bmpCreated) {
        // 初回 BMP
        CreateBmpControls(hwnd);
        g_bmpCreated = true;
        g_fileType = FT_BMP;
        ShowBmpControls(true);
        LayoutBmpControls(hwnd);
    }

    lstrcpyW(g_filePath, path);
    g_fileOpen = true;

    if (isBmp) OpenBmpFile(hwnd, path);
    else       OpenTextFile(hwnd, path);
}

// ================================================================
// Window procedure
// ================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HMENU hMenu = CreateMenu();
        HMENU hFile = CreatePopupMenu();
        AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN,
            L"\u958b\u304f(&O)...\tCtrl+O");
        AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE,
            L"\u4fdd\u5b58(&S)\tCtrl+S");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile,
            L"\u30d5\u30a1\u30a4\u30eb(&F)");
        SetMenu(hwnd, hMenu);
        return 0;
    }

    case WM_SIZE:
        if (g_fileType == FT_TEXT) LayoutEditorControls(hwnd);
        else if (g_fileType == FT_BMP) LayoutBmpControls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_OPEN:
            DoFileOpen(hwnd);
            break;
        case IDM_FILE_SAVE:
            SaveCurrentFile(hwnd);
            break;
        case IDC_BMP_BITCOUNT:
            if (HIWORD(wParam) == CBN_SELCHANGE && g_fileType == FT_BMP) {
                int sel = (int)SendMessageW(
                    g_hBmpVal[BMP_BITCOUNT_IDX], CB_GETCURSEL, 0, 0);
                int bppMap[4] = { 1, 4, 8, 24 };
                if (sel >= 0 && sel < 4 && !g_bmpData.empty()) {
                    g_bmpData = ConvertBmpBpp(g_bmpData, bppMap[sel]);
                    UpdateBmpHeaderDisplay();
                    if (g_hBmpCanvas)
                        InvalidateRect(g_hBmpCanvas, NULL, TRUE);
                }
            }
            break;
        }
        return 0;

    case WM_DESTROY:
        if (g_hEditFont) { DeleteObject(g_hEditFont); g_hEditFont = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ================================================================
// Entry point
// ================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow)
{
    g_hInst = hInst;

    // BMP キャンバスクラスの登録
    WNDCLASSW wcc;
    ZeroMemory(&wcc, sizeof(wcc));
    wcc.lpfnWndProc   = BmpCanvasProc;
    wcc.hInstance     = hInst;
    wcc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcc.lpszClassName = L"ObjeqtNoteBmpCanvas";
    wcc.hCursor       = LoadCursor(NULL, IDC_CROSS);
    if (!RegisterClassW(&wcc)) return 1;

    // メインウィンドウクラスの登録
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ObjeqtNoteWnd";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 1;

    g_hwnd = CreateWindowW(
        L"ObjeqtNoteWnd", L"ObjeqtNote",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    // キーボードアクセラレータ
    ACCEL accel[2];
    accel[0].fVirt = FVIRTKEY | FCONTROL; accel[0].key = 'O'; accel[0].cmd = IDM_FILE_OPEN;
    accel[1].fVirt = FVIRTKEY | FCONTROL; accel[1].key = 'S'; accel[1].cmd = IDM_FILE_SAVE;
    HACCEL hAccel = CreateAcceleratorTableW(accel, 2);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!hAccel || !TranslateAcceleratorW(g_hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
