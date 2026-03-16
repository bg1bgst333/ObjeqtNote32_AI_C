/*
 * ObjeqtNote - Win32+C++03 text editor
 *
 * .txtファイルを開いた場合:
 *   縦に 文字コードCB / BOM CB / エディット / 改行コードCB を配置し
 *   読み込み・編集・保存ができる。
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
static WCHAR     g_filePath[MAX_PATH] = { 0 };

static const WCHAR* ENC_NAMES[ENC_COUNT] = {
    L"SJIS", L"UTF-8", L"UTF-16LE", L"UTF-16BE", L"JIS", L"EUC"
};
static const WCHAR* BOM_NAMES[BOM_COUNT] = {
    L"なし", L"UTF-8 BOM", L"UTF-16LE BOM", L"UTF-16BE BOM"
};
static const WCHAR* EOL_NAMES[EOL_COUNT] = {
    L"CRLF", L"LF", L"CR"
};

static const unsigned char BOM_BYTES_UTF8[]    = { 0xEF, 0xBB, 0xBF };
static const unsigned char BOM_BYTES_UTF16LE[] = { 0xFF, 0xFE };
static const unsigned char BOM_BYTES_UTF16BE[] = { 0xFE, 0xFF };

// ================================================================
// File I/O
// ================================================================
typedef std::vector<unsigned char> ByteVec;

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
            if (i+1 < text.size() && text[i+1] == L'\n') ++i; // CRLF → skip \n
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
// Control creation / layout
// ================================================================
static void CreateEditorControls(HWND hwnd)
{
    HFONT hGui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // ラベル
    g_hLabelEnc = CreateWindowW(L"STATIC", L"文字コード:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_ENC, g_hInst, NULL);
    g_hLabelBom = CreateWindowW(L"STATIC", L"BOM:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_BOM, g_hInst, NULL);
    g_hLabelEol = CreateWindowW(L"STATIC", L"改行コード:",
        WS_CHILD | SS_RIGHT, 0,0,1,1, hwnd, (HMENU)IDC_LABEL_EOL, g_hInst, NULL);

    // コンボボックス
    g_hComboEnc = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_ENC, g_hInst, NULL);
    g_hComboBom = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_BOM, g_hInst, NULL);
    g_hComboEol = CreateWindowW(L"COMBOBOX", NULL,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0,0,1,1,
        hwnd, (HMENU)IDC_COMBO_EOL, g_hInst, NULL);

    // エディット
    g_hEdit = CreateWindowW(L"EDIT", NULL,
        WS_CHILD | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
        0,0,1,1, hwnd, (HMENU)IDC_EDIT_TEXT, g_hInst, NULL);

    // アイテムを追加
    for (int i = 0; i < ENC_COUNT; ++i)
        SendMessageW(g_hComboEnc, CB_ADDSTRING, 0, (LPARAM)ENC_NAMES[i]);
    for (int i = 0; i < BOM_COUNT; ++i)
        SendMessageW(g_hComboBom, CB_ADDSTRING, 0, (LPARAM)BOM_NAMES[i]);
    for (int i = 0; i < EOL_COUNT; ++i)
        SendMessageW(g_hComboEol, CB_ADDSTRING, 0, (LPARAM)EOL_NAMES[i]);

    // GUI フォント
    SendMessageW(g_hLabelEnc, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hLabelBom, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hLabelEol, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboEnc, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboBom, WM_SETFONT, (WPARAM)hGui, FALSE);
    SendMessageW(g_hComboEol, WM_SETFONT, (WPARAM)hGui, FALSE);

    // エディットは等幅日本語フォント
    g_hEditFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"MS Gothic");
    SendMessageW(g_hEdit, WM_SETFONT,
        (WPARAM)(g_hEditFont ? g_hEditFont : hGui), FALSE);

    // テキスト上限を大きくする
    SendMessageW(g_hEdit, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFF, 0);
}

static void LayoutEditorControls(HWND hwnd)
{
    if (!g_fileOpen) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    const int pad    = 4;
    const int rowH   = 24;
    const int labelW = 90;
    const int comboW = 180;
    const int dropH  = 160; // コンボのドロップダウン部分の高さ

    int y1  = pad;                   // 文字コード行 top
    int y2  = y1 + rowH + pad;       // BOM 行 top
    int yE  = y2 + rowH + pad;       // エディット top
    int y4  = H  - pad - rowH;       // 改行コード行 top
    int yEb = y4 - pad;              // エディット bottom

    if (yEb < yE) yEb = yE;

    int x0 = pad;
    int x1 = x0 + labelW + pad;

    // 文字コード
    MoveWindow(g_hLabelEnc, x0, y1 + 4, labelW, rowH - 4, TRUE);
    MoveWindow(g_hComboEnc, x1, y1,     comboW, rowH + dropH, TRUE);

    // BOM
    MoveWindow(g_hLabelBom, x0, y2 + 4, labelW, rowH - 4, TRUE);
    MoveWindow(g_hComboBom, x1, y2,     comboW, rowH + dropH, TRUE);

    // エディット
    MoveWindow(g_hEdit, 0, yE, W, yEb - yE, TRUE);

    // 改行コード
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
// Open / Save
// ================================================================
static void OpenTextFile(HWND hwnd, const WCHAR* path)
{
    ByteVec data;
    if (!ReadAllBytes(path, data)) {
        MessageBoxW(hwnd, L"ファイルを開けませんでした。",
                    L"エラー", MB_OK | MB_ICONERROR);
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
    if (!g_fileOpen) return;

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
        MessageBoxW(hwnd, L"ファイルを保存できませんでした。",
                    L"エラー", MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(hwnd, L"保存しました。", L"ObjeqtNote", MB_OK);
}

static void DoFileOpen(HWND hwnd)
{
    WCHAR path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter =
        L"テキストファイル (*.txt)\0*.txt\0"
        L"すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"txt";

    if (!GetOpenFileNameW(&ofn)) return;

    if (!g_fileOpen) {
        CreateEditorControls(hwnd);
        g_fileOpen = true;
        LayoutEditorControls(hwnd);
        ShowEditorControls(true);
    }

    lstrcpyW(g_filePath, path);
    OpenTextFile(hwnd, path);
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
        AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"開く(&O)...\tCtrl+O");
        AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE, L"保存(&S)\tCtrl+S");
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"ファイル(&F)");
        SetMenu(hwnd, hMenu);
        return 0;
    }
    case WM_SIZE:
        LayoutEditorControls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_OPEN: DoFileOpen(hwnd);  break;
        case IDM_FILE_SAVE: SaveTextFile(hwnd); break;
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
