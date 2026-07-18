//----------------------------------------------------------------------------------
//	LayerHeightSlider.cpp
//	AviUtl ExEdit2 用 汎用プラグイン (.aux2)
//
//	タイムライン(レイヤー編集)のレイヤー高さを、GUI のスライダーから
//	任意の値に変更できるようにするプラグインです。
//
//	仕組み:
//	  レイヤー高さは外観設定ファイル style.conf の [Layout] セクションの
//	  LayerHeight キーで決まります (本体既定値: 26)。
//	  本体にはレイヤー高さを実行中に変更する公開 API が存在しない為、
//	  本プラグインは style.conf の LayerHeight の「値の部分だけ」を書き換え、
//	  汎用プラグイン API の restart_host_app() で本体を再起動することで
//	  反映します (style.conf は起動時のみ読み込まれます)。
//
//	style.conf の検索優先順位 (「自動」選択時。存在するファイルを編集します):
//	  1. %ProgramData%\aviutl2\style.conf          (SHGetKnownFolderPathで取得)
//	  2. <AviUtl2.exeのあるフォルダ>\Data\style.conf
//	  3. <AviUtl2.exeのあるフォルダ>\style.conf
//	  ※編集対象はコンボボックスから手動選択も出来ます。選択はプラグインと
//	    同じフォルダの LayerHeightSlider.ini に保存され、再起動後も維持されます。
//	  ※書き換えるのは LayerHeight の値のみで、他の行・コメント・改行コードは
//	    バイト単位でそのまま保持します。
//
//	使用 API (すべて公式 SDK aviutl2_sdk のヘッダーに定義されているもの):
//	  plugin2.h : RegisterPlugin() / HOST_APP_TABLE::register_window_client()
//	              HOST_APP_TABLE::create_edit_handle()
//	              EDIT_HANDLE::restart_host_app()
//	  config2.h : CONFIG_HANDLE::get_layout_size()  ([Layout] の値の読み取り)
//	              CONFIG_HANDLE::get_color_code()   ([Color]  の値の読み取り)
//	              CONFIG_HANDLE::get_font_info()    ([Font]   の値の読み取り)
//	              CONFIG_HANDLE::translate()        (言語ファイル対応)
//	  logger2.h : LOG_HANDLE (本体のログウィンドウへの出力)
//
//	ビルド: Visual Studio 2022 / x64 / Unicode / DLL (拡張子を .aux2 にする)
//	配置  : %ProgramData%\aviutl2\Plugin\LayerHeightSlider.aux2
//----------------------------------------------------------------------------------

#include <windows.h>
#include <commctrl.h>	// トラックバー(スライダー)コントロール
#include <shlobj.h>		// SHGetKnownFolderPath / FOLDERID_ProgramData
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>		// strlen

#include "plugin2.h"	// 汎用プラグイン API (公式SDK)
#include "logger2.h"	// ログ出力 API (公式SDK)
#include "config2.h"	// 設定関連 API (公式SDK)

// リンクするライブラリ (プロジェクト設定不要でビルド出来るように pragma で指定)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")	// SHGetKnownFolderPath
#pragma comment(lib, "ole32.lib")	// CoTaskMemFree
#pragma comment(lib, "uuid.lib")	// FOLDERID_ProgramData (KNOWNFOLDERIDのGUID実体)

//----------------------------------------------------------------------------------
//	定数定義
//----------------------------------------------------------------------------------

#define PLUGIN_WINDOW_NAME	L"レイヤー高さ設定"		// ウィンドウクライアント名 (本体のウィンドウ一覧に表示されます)

// コントロール ID
#define IDC_TRACKBAR		1001	// レイヤー高さのスライダー
#define IDC_PRESET_SMALL	1002	// プリセット「小」ボタン
#define IDC_PRESET_MEDIUM	1003	// プリセット「中」ボタン
#define IDC_PRESET_LARGE	1004	// プリセット「大」ボタン
#define IDC_APPLY			1005	// 「適用して再起動」ボタン
#define IDC_CONF_TARGET		1006	// 編集対象style.conf選択コンボボックス

// レイヤー高さの可変範囲 (ピクセル)
// 小さすぎるとレイヤー名やオブジェクトが描画出来なくなる為に下限を設けています
constexpr int LAYER_HEIGHT_MIN = 10;
constexpr int LAYER_HEIGHT_MAX = 120;

// 本体の既定値 (style.conf 未設定時に [Layout] LayerHeight が取得出来ない場合の保険)
constexpr int LAYER_HEIGHT_DEFAULT = 26;

// プリセット値 (本プラグイン独自の目安です)
constexpr int PRESET_SMALL  = 18;	// 小
constexpr int PRESET_MEDIUM = 26;	// 中 (本体既定値)
constexpr int PRESET_LARGE  = 36;	// 大

//----------------------------------------------------------------------------------
//	グローバル変数
//----------------------------------------------------------------------------------

static EDIT_HANDLE*		g_edit    = nullptr;	// 編集ハンドル (restart_host_app に使用)
static LOG_HANDLE*		g_logger  = nullptr;	// ログ出力ハンドル
static CONFIG_HANDLE*	g_config  = nullptr;	// 設定ハンドル

static HWND		g_hwnd          = nullptr;	// プラグインウィンドウ
static HWND		g_trackbar      = nullptr;	// スライダー
static HWND		g_label_value   = nullptr;	// 現在値表示ラベル
static HWND		g_label_path    = nullptr;	// 編集対象のstyle.confパス表示ラベル
static HWND		g_label_note    = nullptr;	// 注意書きラベル
static HWND		g_btn_small     = nullptr;	// プリセットボタン
static HWND		g_btn_medium    = nullptr;
static HWND		g_btn_large     = nullptr;
static HWND		g_btn_apply     = nullptr;	// 適用ボタン
static HWND		g_combo_conf    = nullptr;	// 編集対象style.conf選択コンボボックス

static HINSTANCE g_hinst        = nullptr;	// 本プラグインDLLのインスタンスハンドル (DllMainで設定)

static HFONT	g_font          = nullptr;	// 本体設定に合わせたフォント
static HBRUSH	g_brush_bg      = nullptr;	// 本体設定に合わせた背景ブラシ
static COLORREF	g_color_bg      = RGB(32, 32, 32);		// 背景色 (フォールバック値)
static COLORREF	g_color_text    = RGB(240, 240, 240);	// 文字色 (フォールバック値)

static int		g_height        = LAYER_HEIGHT_DEFAULT;	// 現在のレイヤー高さ
static int		g_height_saved  = LAYER_HEIGHT_DEFAULT;	// 起動時(=本体に反映済み)のレイヤー高さ

//----------------------------------------------------------------------------------
//	ユーティリティ
//----------------------------------------------------------------------------------

// style.conf の色コード(0xRRGGBB)を Win32 の COLORREF(0x00BBGGRR) に変換します
static COLORREF ColorCodeToColorref(int code) {
	return RGB((code >> 16) & 0xFF, (code >> 8) & 0xFF, code & 0xFF);
}

// ファイルが存在するか (ディレクトリは除外)
static bool FileExistsW(const std::wstring& path) {
	const DWORD attr = GetFileAttributesW(path.c_str());
	return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

//----------------------------------------------------------------------------------
//	style.conf の検索処理
//----------------------------------------------------------------------------------

// ProgramData フォルダ (通常は C:\ProgramData) のパスを取得します
// SHGetKnownFolderPath を優先し、失敗した場合は環境変数 ProgramData を参照します
static std::wstring GetProgramDataDir() {
	std::wstring result;

	// 方法1: SHGetKnownFolderPath (推奨API)
	PWSTR p = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &p)) && p) {
		result = p;
	}
	if (p) CoTaskMemFree(p);	// 成否に関わらず解放が必要

	// 方法2: 環境変数 ProgramData (フォールバック)
	if (result.empty()) {
		wchar_t buf[1024];
		const DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, 1024);
		if (n > 0 && n < 1024) result = buf;
	}
	return result;
}

// AviUtl2.exe (ホストアプリ本体) のあるフォルダを取得します
// 本プラグインDLLはAviUtl2のプロセス内にロードされる為、
// GetModuleFileNameW(nullptr) でプロセスの実行ファイルパスが取得出来ます
static std::wstring GetHostExeDir() {
	wchar_t buf[1024];
	const DWORD n = GetModuleFileNameW(nullptr, buf, 1024);
	if (n == 0 || n >= 1024) return L"";
	std::wstring path(buf, n);
	const size_t sep = path.find_last_of(L'\\');
	if (sep == std::wstring::npos) return L"";
	return path.substr(0, sep);	// ファイル名部分を除去してフォルダパスにする
}

// 編集対象の選択モード (コンボボックスの項目の並び順と一致させます)
constexpr int CONF_MODE_AUTO        = 0;	// 自動 (優先順位で選択)
constexpr int CONF_MODE_PROGRAMDATA = 1;	// %ProgramData%\aviutl2\style.conf
constexpr int CONF_MODE_DATA        = 2;	// <AviUtl2.exeのフォルダ>\Data\style.conf
constexpr int CONF_MODE_EXE         = 3;	// <AviUtl2.exeのフォルダ>\style.conf

static int g_conf_mode = CONF_MODE_AUTO;	// 現在の編集対象モード (iniに保存されます)

// 指定モードの style.conf のパスを返します (CONF_MODE_AUTOは対象外)
// パスが取得出来ない場合は空文字を返します
static std::wstring GetCandidatePath(int mode) {
	switch (mode) {
	case CONF_MODE_PROGRAMDATA: {
		const std::wstring pd = GetProgramDataDir();
		return pd.empty() ? std::wstring() : pd + L"\\aviutl2\\style.conf";
	}
	case CONF_MODE_DATA: {
		const std::wstring exe = GetHostExeDir();
		return exe.empty() ? std::wstring() : exe + L"\\Data\\style.conf";
	}
	case CONF_MODE_EXE: {
		const std::wstring exe = GetHostExeDir();
		return exe.empty() ? std::wstring() : exe + L"\\style.conf";
	}
	}
	return L"";
}

// 編集対象の style.conf のパスを決定します
// 手動選択されている場合はそのパスを、自動の場合は優先順位に従って決定します
//   1. %ProgramData%\aviutl2\style.conf          (存在する場合)
//   2. <AviUtl2.exeのフォルダ>\Data\style.conf   (存在する場合)
//   3. <AviUtl2.exeのフォルダ>\style.conf        (最終フォールバック)
// 取得に失敗した場合は空文字を返します
static std::wstring FindStyleConfPath() {
	// 手動選択されている場合はそのパスをそのまま返す
	if (g_conf_mode != CONF_MODE_AUTO) {
		const std::wstring manual = GetCandidatePath(g_conf_mode);
		if (!manual.empty()) return manual;
		// パスが取得出来なかった場合は自動選択にフォールバックする
	}

	// 優先順位1: ProgramData 版
	const std::wstring program_data = GetProgramDataDir();
	std::wstring pd_conf;
	if (!program_data.empty()) {
		pd_conf = program_data + L"\\aviutl2\\style.conf";
		if (FileExistsW(pd_conf)) return pd_conf;
	}

	// 優先順位2: exeフォルダ\Data 版
	const std::wstring exe_dir = GetHostExeDir();
	if (!exe_dir.empty()) {
		const std::wstring data_conf = exe_dir + L"\\Data\\style.conf";
		if (FileExistsW(data_conf)) return data_conf;

		// 優先順位3: exeフォルダ直下版 (存在しなくてもこれを編集対象とする)
		return exe_dir + L"\\style.conf";
	}

	// exeフォルダが取得出来なかった場合の保険 (通常は到達しません)
	return pd_conf;
}

//----------------------------------------------------------------------------------
//	プラグイン設定 (編集対象の選択) の保存・読み込み
//----------------------------------------------------------------------------------

// プラグイン設定ファイルのパスを返します
// プラグインDLLと同じフォルダの LayerHeightSlider.ini になります
// (通常は C:\ProgramData\aviutl2\Plugin\LayerHeightSlider.ini)
static std::wstring GetSettingsIniPath() {
	wchar_t buf[1024];
	const DWORD n = GetModuleFileNameW(g_hinst, buf, 1024);
	if (n == 0 || n >= 1024) return L"";
	std::wstring path(buf, n);
	// 拡張子(.aux2)を .ini に差し替える
	const size_t dot = path.find_last_of(L'.');
	const size_t sep = path.find_last_of(L'\\');
	if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) {
		return path + L".ini";
	}
	return path.substr(0, dot) + L".ini";
}

// 編集対象モードを ini から読み込みます (未設定・不正値の場合は自動選択)
static void LoadConfMode() {
	const std::wstring ini = GetSettingsIniPath();
	if (ini.empty()) return;
	int mode = (int)GetPrivateProfileIntW(L"Settings", L"ConfTarget", CONF_MODE_AUTO, ini.c_str());
	if (mode < CONF_MODE_AUTO || mode > CONF_MODE_EXE) mode = CONF_MODE_AUTO;
	g_conf_mode = mode;
}

// 編集対象モードを ini に保存します (AviUtl2を再起動しても選択が維持されます)
static void SaveConfMode() {
	const std::wstring ini = GetSettingsIniPath();
	if (ini.empty()) return;
	wchar_t val[16];
	wsprintfW(val, L"%d", g_conf_mode);
	WritePrivateProfileStringW(L"Settings", L"ConfTarget", val, ini.c_str());
}

//----------------------------------------------------------------------------------
//	style.conf の書き換え処理
//----------------------------------------------------------------------------------

// バッファ内の [Layout] セクションの LayerHeight の「値の部分だけ」を書き換えます。
// 他の行・コメント行・改行コード(CRLF/LF混在含む)・BOM・末尾改行の有無は
// バイト単位で一切変更しません。
//
// content	: style.conf の全内容 (バイト列のまま。書き換え結果もここに反映)
// height	: 設定するレイヤー高さ
// 戻り値	: 書き換え(またはキーが無い場合の最小限の追記)が出来た場合 true
static bool PatchLayerHeight(std::string& content, int height) {
	const std::string num = std::to_string(height);
	const size_t n = content.size();

	// UTF-8 BOM があれば読み飛ばす (BOM自体は書き換えず保持されます)
	size_t pos = 0;
	if (n >= 3 &&
		(unsigned char)content[0] == 0xEF &&
		(unsigned char)content[1] == 0xBB &&
		(unsigned char)content[2] == 0xBF) {
		pos = 3;
	}

	bool   in_layout = false;					// 現在 [Layout] セクション内か
	size_t layout_insert_pos = std::string::npos;	// キーが無い場合の追記位置 ([Layout]行の次行頭)
	bool   layout_header_has_eol = true;			// [Layout]行に改行があったか

	// 行単位で走査する (contentのコピーや再構築はせず、位置の計算のみ行う)
	while (pos <= n) {
		// 行末を求める: eol='\n'の位置(無ければEOF) / text_end=行の実テキスト終端('\r'を除く)
		const size_t eol = content.find('\n', pos);
		const size_t line_end = (eol == std::string::npos) ? n : eol;
		size_t text_end = line_end;
		if (text_end > pos && content[text_end - 1] == '\r') text_end--;

		// 行頭の空白をスキップ
		size_t i = pos;
		while (i < text_end && (content[i] == ' ' || content[i] == '\t')) i++;

		if (i < text_end && content[i] == '[') {
			// セクション行 "[...]"
			in_layout = (content.compare(i, 8, "[Layout]") == 0);
			if (in_layout) {
				layout_insert_pos = (eol == std::string::npos) ? n : eol + 1;
				layout_header_has_eol = (eol != std::string::npos);
			}
		} else if (in_layout && i < text_end && content[i] != ';') {
			// [Layout] セクション内の非コメント行: "LayerHeight = 値" 形式かを判定
			// ※"LayerHeaderWidth"等の別キーは下記の比較で自然に除外されます
			static const char key[] = "LayerHeight";
			constexpr size_t keylen = sizeof(key) - 1;
			if (text_end - i >= keylen && content.compare(i, keylen, key) == 0) {
				size_t j = i + keylen;
				// キー名と '=' の間の空白は許容 (そのまま保持)
				while (j < text_end && (content[j] == ' ' || content[j] == '\t')) j++;
				if (j < text_end && content[j] == '=') {
					j++;
					// '=' の直後の空白も許容 (そのまま保持)
					while (j < text_end && (content[j] == ' ' || content[j] == '\t')) j++;
					// ここから行末までが「値」→ この範囲だけを新しい値に差し替える
					// (行末の改行コードや以降の内容には一切触りません)
					content.replace(j, text_end - j, num);
					return true;
				}
			}
		}
		// コメント行(';')やその他の行はそのまま (何もしない)

		if (eol == std::string::npos) break;	// 最終行まで走査した
		pos = eol + 1;
	}

	//------------------------------------------------------------------
	// ここに来るのは LayerHeight キーが見つからなかった場合のみ。
	// 既存の内容は一切変更せず、動作に必要な最小限の1行だけを追記します。
	// (既定のstyle.confにはLayerHeightが存在する為、通常は実行されません)
	//------------------------------------------------------------------

	// 追記に使う改行コードはファイル内の既存の改行コードに合わせる
	const std::string eol_str =
		(content.find("\r\n") != std::string::npos || content.empty()) ? "\r\n" : "\n";
	const std::string new_line = "LayerHeight=" + num + eol_str;

	if (layout_insert_pos != std::string::npos) {
		// [Layout] セクションは在るがキーが無い → セクション先頭に1行追記
		if (!layout_header_has_eol) {
			// [Layout] がファイル末尾で改行無しの場合は改行を補ってから追記
			content += eol_str + "LayerHeight=" + num + eol_str;
		} else {
			content.insert(layout_insert_pos, new_line);
		}
	} else {
		// [Layout] セクション自体が無い → 末尾にセクションごと追記
		std::string tail;
		if (!content.empty() && content.back() != '\n') tail += eol_str;	// 末尾に改行が無い場合のみ補う
		tail += "[Layout]" + eol_str + new_line;
		content += tail;
	}
	return true;
}

// 編集対象パス表示の更新 (定義はUI更新処理のセクションにあります)
static void UpdatePathLabel();

// style.conf を検索し、LayerHeight の値のみを書き換えて保存します
// 手順: ファイル全体をバイト列で読み込み → 該当の値部分だけ差し替え → 全体を書き戻し
static bool SaveLayerHeight(int height) {
	// 対象パスが変化している可能性がある為、保存操作の度に表示を更新する
	UpdatePathLabel();

	const std::wstring path = FindStyleConfPath();
	if (path.empty()) {
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: style.conf の場所を特定出来ませんでした");
		return false;
	}

	// 既存ファイルをバイナリのまま読み込む (改行コード等を変換しない)
	// ※優先順位3のフォールバック先が未作成の場合のみ空(新規作成)になります
	std::string content;
	{
		std::ifstream ifs(path.c_str(), std::ios::binary);
		if (ifs) {
			std::ostringstream ss;
			ss << ifs.rdbuf();
			content = ss.str();
		}
	}

	// LayerHeight の値部分のみを書き換える
	if (!PatchLayerHeight(content, height)) {
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: style.conf の書き換えに失敗しました");
		return false;
	}

	// 書き戻し (バイナリのまま。他の内容はバイト単位で保持されています)
	std::ofstream ofs(path.c_str(), std::ios::binary | std::ios::trunc);
	if (!ofs) {
		// 本体フォルダが Program Files 配下の場合等、書き込み権限が無いと失敗します
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: style.conf を書き込めません (アクセス権限を確認してください)");
		return false;
	}
	ofs.write(content.data(), (std::streamsize)content.size());
	ofs.close();
	if (ofs.fail()) {
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: style.conf の書き込み中にエラーが発生しました");
		return false;
	}

	if (g_logger) {
		// wsprintfW は最大1024文字(終端含む)書き込む為、バッファは1024確保する
		wchar_t msg[1024];
		wsprintfW(msg, L"LayerHeightSlider: LayerHeight=%d を保存しました (%s)", height, path.c_str());
		g_logger->log(g_logger, msg);
	}
	return true;
}

//----------------------------------------------------------------------------------
//	UI 更新処理
//----------------------------------------------------------------------------------

// 現在値表示ラベルを更新します
// 本体に反映済みの値と異なる場合は「(再起動で反映)」を付けて知らせます
static void UpdateValueLabel() {
	// wsprintfW は最大1024文字(終端含む)書き込む為、バッファは1024確保する
	wchar_t text[1024];
	if (g_height != g_height_saved) {
		wsprintfW(text, L"%s: %d px  (%s)",
			g_config->translate(g_config, L"レイヤーの高さ"), g_height,
			g_config->translate(g_config, L"再起動で反映"));
	} else {
		wsprintfW(text, L"%s: %d px",
			g_config->translate(g_config, L"レイヤーの高さ"), g_height);
	}
	SetWindowTextW(g_label_value, text);
}

// 編集対象の style.conf のフルパスをラベルに表示します
// (ProgramData版のstyle.confが後から作成・削除された場合等に編集対象が
//  変わる可能性がある為、初期化時と保存の度に再解決して更新します)
static void UpdatePathLabel() {
	if (!g_label_path) return;	// コントロール作成前に呼ばれた場合は何もしない
	const std::wstring path = FindStyleConfPath();
	std::wstring text = g_config->translate(g_config, L"編集対象");
	text += L": ";
	text += path.empty() ? g_config->translate(g_config, L"(取得失敗)") : path;
	// 対象ファイルが未作成の場合は分かるように表示する (次回保存時に作成されます)
	if (!path.empty() && !FileExistsW(path)) {
		text += L" ";
		text += g_config->translate(g_config, L"(未作成)");
	}
	SetWindowTextW(g_label_path, text.c_str());
}

// スライダー位置と表示を指定値に揃え、style.conf に保存します
static void SetHeight(int height, bool save) {
	// 範囲内に丸める
	if (height < LAYER_HEIGHT_MIN) height = LAYER_HEIGHT_MIN;
	if (height > LAYER_HEIGHT_MAX) height = LAYER_HEIGHT_MAX;
	g_height = height;

	SendMessageW(g_trackbar, TBM_SETPOS, TRUE, height);
	UpdateValueLabel();

	if (save) SaveLayerHeight(height);
}

// 子コントロールを親ウィンドウのサイズに合わせて再配置します
static void LayoutControls(int client_width) {
	// 本体の設定に合わせたコントロール高さを使用 (取得出来ない場合は22)
	int item_h = g_config->get_layout_size(g_config, "SettingItemHeight");
	if (item_h <= 0) item_h = 22;

	const int margin = 10;					// 全体の余白
	const int w = client_width - margin * 2;	// コントロール幅
	if (w <= 0) return;
	int y = margin;

	// 1行目: 現在値ラベル
	MoveWindow(g_label_value, margin, y, w, item_h, TRUE);
	y += item_h + 4;

	// 2行目: スライダー
	MoveWindow(g_trackbar, margin, y, w, item_h + 8, TRUE);
	y += item_h + 12;

	// 3行目: プリセットボタン (小・中・大 を等幅で配置)
	const int gap = 6;
	const int bw = (w - gap * 2) / 3;
	MoveWindow(g_btn_small,  margin,                y, bw, item_h + 4, TRUE);
	MoveWindow(g_btn_medium, margin + (bw + gap),   y, bw, item_h + 4, TRUE);
	MoveWindow(g_btn_large,  margin + (bw + gap)*2, y, bw, item_h + 4, TRUE);
	y += item_h + 12;

	// 4行目: 適用ボタン
	MoveWindow(g_btn_apply, margin, y, w, item_h + 6, TRUE);
	y += item_h + 12;

	// 5行目: 編集対象の style.conf 選択コンボボックス
	// ※高さにはドロップダウン展開時のリスト分を含めて指定します (表示部の高さは自動)
	MoveWindow(g_combo_conf, margin, y, w, item_h * 8, TRUE);
	y += item_h + 10;

	// 6行目: 編集対象の style.conf パス (幅が足りない場合は中央を「...」で省略表示)
	MoveWindow(g_label_path, margin, y, w, item_h, TRUE);
	y += item_h + 4;

	// 7行目: 注意書き
	MoveWindow(g_label_note, margin, y, w, item_h * 2, TRUE);
}

//----------------------------------------------------------------------------------
//	ウィンドウプロシージャ
//----------------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	switch (message) {

	case WM_SIZE:
		// ドッキング先のサイズに合わせてコントロールを再配置
		LayoutControls(LOWORD(lparam));
		return 0;

	case WM_HSCROLL:
		// スライダー操作
		if ((HWND)lparam == g_trackbar) {
			const int pos  = (int)SendMessageW(g_trackbar, TBM_GETPOS, 0, 0);
			const int code = LOWORD(wparam);
			// 表示は常に即時更新する
			g_height = pos;
			UpdateValueLabel();
			// ドラッグ中(TB_THUMBTRACK)はファイル書き込みせず、
			// 操作確定時(ドラッグ終了・クリック・キー操作)のみ保存して軽量化
			if (code != TB_THUMBTRACK) {
				SaveLayerHeight(g_height);
			}
			return 0;
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case IDC_PRESET_SMALL:
			SetHeight(PRESET_SMALL, true);
			SetFocus(NULL);	// フォーカスを外して本体のショートカットキーを妨げない (公式サンプル準拠)
			return 0;
		case IDC_PRESET_MEDIUM:
			SetHeight(PRESET_MEDIUM, true);
			SetFocus(NULL);
			return 0;
		case IDC_PRESET_LARGE:
			SetHeight(PRESET_LARGE, true);
			SetFocus(NULL);
			return 0;
		case IDC_APPLY:
			// 念のため現在値を保存してから本体を再起動する
			SetFocus(NULL);
			if (SaveLayerHeight(g_height)) {
				const int ret = MessageBoxW(hwnd,
					g_config->translate(g_config,
						L"設定を反映する為にAviUtl ExEdit2を再起動します。\r\n"
						L"プロジェクトに未保存の変更がある場合は先に保存してください。\r\n\r\n"
						L"再起動しますか？"),
					PLUGIN_WINDOW_NAME,
					MB_OKCANCEL | MB_ICONINFORMATION);
				if (ret == IDOK && g_edit) {
					// 公式APIによる本体の再起動 (再起動後に新しいLayerHeightが読み込まれます)
					g_edit->restart_host_app();
				}
			}
			return 0;
		case IDC_CONF_TARGET:
			// 編集対象 style.conf の選択変更
			if (HIWORD(wparam) == CBN_SELCHANGE) {
				const int sel = (int)SendMessageW(g_combo_conf, CB_GETCURSEL, 0, 0);
				if (sel >= CONF_MODE_AUTO && sel <= CONF_MODE_EXE && sel != g_conf_mode) {
					bool accept = true;
					// 手動選択したファイルが未作成の場合は新規作成して良いか確認する
					// (優先順位の高い場所に最小限のファイルを新規作成すると、優先順位の
					//  低い場所にある既存のstyle.confが読み込まれなくなる恐れがある為)
					if (sel != CONF_MODE_AUTO) {
						const std::wstring path = GetCandidatePath(sel);
						if (!path.empty() && !FileExistsW(path)) {
							std::wstring msg = path + L"\r\n\r\n";
							msg += g_config->translate(g_config,
								L"選択したstyle.confはまだ存在しません。次回の保存時に新規作成されます。\r\n"
								L"※新規作成されるファイルにはLayerHeightのみが記述されます。\r\n"
								L"　優先順位の高い場所に新規作成すると、優先順位の低い場所にある\r\n"
								L"　既存のstyle.confの設定が使われなくなる場合があります。\r\n\r\n"
								L"このファイルを編集対象にしますか？");
							accept = (MessageBoxW(hwnd, msg.c_str(), PLUGIN_WINDOW_NAME,
								MB_OKCANCEL | MB_ICONWARNING) == IDOK);
						}
					}
					if (accept) {
						g_conf_mode = sel;
						SaveConfMode();	// 選択をiniに保存 (再起動後も維持されます)
					} else {
						// キャンセル時は選択表示を元に戻す
						SendMessageW(g_combo_conf, CB_SETCURSEL, g_conf_mode, 0);
					}
					UpdatePathLabel();	// 表示パスを新しい対象に更新
				}
				SetFocus(NULL);	// フォーカスを外して本体のショートカットキーを妨げない
			}
			return 0;
		}
		break;

	case WM_CTLCOLORSTATIC:
		// ラベルの配色を本体のテーマ(style.confの[Color])に合わせる
		SetTextColor((HDC)wparam, g_color_text);
		SetBkMode((HDC)wparam, TRANSPARENT);
		return (LRESULT)g_brush_bg;

	case WM_ERASEBKGND: {
		// 背景を本体のテーマ色で塗りつぶす
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wparam, &rc, g_brush_bg);
		return 1;
	}

	}
	return DefWindowProcW(hwnd, message, wparam, lparam);
}

//----------------------------------------------------------------------------------
//	プラグイン情報
//----------------------------------------------------------------------------------

static COMMON_PLUGIN_TABLE common_plugin_table = {
	L"レイヤー高さ設定",											// プラグインの名前
	L"Layer Height Slider version 1.3.0 (style.confのLayerHeightをGUIから変更します)",	// プラグインの情報
};

//----------------------------------------------------------------------------------
//	エクスポート関数 (AviUtl ExEdit2 から呼び出されるエントリポイント群)
//----------------------------------------------------------------------------------

// 汎用プラグイン構造体のポインタを渡す関数
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
	return &common_plugin_table;
}

// 必要とする本体バージョン番号 (公式サンプル WindowClient.cpp と同じ値)
EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
	return 2003300;
}

// ログ出力機能初期化関数 ※InitializePlugin()より先に呼ばれます
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	g_logger = handle;
}

// 設定関連機能初期化関数 ※InitializePlugin()より先に呼ばれます
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
	g_config = handle;
}

// プラグインDLL初期化関数
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	return true;
}

// プラグインDLL終了関数 (GDIリソースの後始末)
EXTERN_C __declspec(dllexport) void UninitializePlugin() {
	if (g_font)     { DeleteObject(g_font);     g_font = nullptr; }
	if (g_brush_bg) { DeleteObject(g_brush_bg); g_brush_bg = nullptr; }
}

//----------------------------------------------------------------------------------
//	プラグイン登録関数 (必須)
//----------------------------------------------------------------------------------

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	// トラックバーコントロールを使う為に Common Controls を初期化
	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);

	// 前回選択した編集対象モードを ini から復元する
	LoadConfMode();

	//------------------------------------------------------------------
	// 本体のテーマ (style.conf) から配色・フォントを取得してUIに馴染ませる
	//------------------------------------------------------------------
	{
		const int bg   = g_config->get_color_code(g_config, "Background");	// [Color] Background
		const int text = g_config->get_color_code(g_config, "Text");		// [Color] Text
		// 両方とも 0 (取得失敗) の場合のみフォールバック値のままにする
		if (bg != 0 || text != 0) {
			g_color_bg   = ColorCodeToColorref(bg);
			g_color_text = ColorCodeToColorref(text);
		}
		g_brush_bg = CreateSolidBrush(g_color_bg);

		// [Font] Control のフォントでUIを描画する
		FONT_INFO* fi = g_config->get_font_info(g_config, "Control");
		g_font = CreateFontW(
			-(int)(fi->size + 0.5f),				// 高さ (負値=文字の高さ指定)
			0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
			fi->name);
	}

	//------------------------------------------------------------------
	// 現在のレイヤー高さを取得してスライダーの初期値にする
	// (style.conf 未設定時は本体既定値が返り、設定済みならその値が返ります)
	//------------------------------------------------------------------
	int current = g_config->get_layout_size(g_config, "LayerHeight");
	if (current <= 0) current = LAYER_HEIGHT_DEFAULT;	// 取得失敗時の保険
	if (current < LAYER_HEIGHT_MIN) current = LAYER_HEIGHT_MIN;
	if (current > LAYER_HEIGHT_MAX) current = LAYER_HEIGHT_MAX;
	g_height       = current;
	g_height_saved = current;

	//------------------------------------------------------------------
	// プラグインウィンドウの作成
	//------------------------------------------------------------------
	WNDCLASSEXW wcex = {};
	wcex.cbSize        = sizeof(wcex);
	wcex.lpszClassName = L"LayerHeightSliderWindow";
	wcex.lpfnWndProc   = WndProc;
	wcex.hInstance     = GetModuleHandleW(0);
	wcex.hbrBackground = nullptr;	// 背景は WM_ERASEBKGND でテーマ色を描画
	wcex.hCursor       = LoadCursorW(NULL, IDC_ARROW);
	if (!RegisterClassExW(&wcex)) {
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: ウィンドウクラスの登録に失敗しました");
		return;
	}

	// 公式サンプル準拠: 親無しでWS_CHILDは作れない為、一旦WS_POPUPで作成します
	// (register_window_client 側で WS_CHILD が追加され親が設定されます)
	g_hwnd = CreateWindowExW(
		0, wcex.lpszClassName, PLUGIN_WINDOW_NAME, WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, 260, 200,
		nullptr, nullptr, GetModuleHandleW(0), nullptr);
	if (!g_hwnd) {
		if (g_logger) g_logger->error(g_logger, L"LayerHeightSlider: ウィンドウの作成に失敗しました");
		return;
	}

	//------------------------------------------------------------------
	// 子コントロールの作成 (位置・サイズは WM_SIZE で調整されます)
	//------------------------------------------------------------------

	// 現在値表示ラベル
	g_label_value = CreateWindowExW(
		0, WC_STATICW, L"", WS_VISIBLE | WS_CHILD | SS_LEFT,
		0, 0, 0, 0, g_hwnd, nullptr, GetModuleHandleW(0), nullptr);

	// レイヤー高さスライダー
	// TBS_NOTICKS: 目盛り無しのフラットな外観 / TBS_TOOLTIPS: ドラッグ中に値をツールチップ表示
	g_trackbar = CreateWindowExW(
		0, TRACKBAR_CLASSW, L"", WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_TRACKBAR, GetModuleHandleW(0), nullptr);
	SendMessageW(g_trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(LAYER_HEIGHT_MIN, LAYER_HEIGHT_MAX));
	SendMessageW(g_trackbar, TBM_SETPAGESIZE, 0, 5);	// PageUp/Down・クリック時の移動量
	SendMessageW(g_trackbar, TBM_SETPOS, TRUE, g_height);

	// プリセットボタン (小・中・大)
	g_btn_small = CreateWindowExW(
		0, WC_BUTTONW, g_config->translate(g_config, L"小"),
		WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_PRESET_SMALL, GetModuleHandleW(0), nullptr);
	g_btn_medium = CreateWindowExW(
		0, WC_BUTTONW, g_config->translate(g_config, L"中(既定)"),
		WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_PRESET_MEDIUM, GetModuleHandleW(0), nullptr);
	g_btn_large = CreateWindowExW(
		0, WC_BUTTONW, g_config->translate(g_config, L"大"),
		WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_PRESET_LARGE, GetModuleHandleW(0), nullptr);

	// 適用ボタン
	g_btn_apply = CreateWindowExW(
		0, WC_BUTTONW, g_config->translate(g_config, L"適用して再起動"),
		WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_APPLY, GetModuleHandleW(0), nullptr);

	// 編集対象の style.conf 選択コンボボックス
	// CBS_DROPDOWNLIST: リストからの選択のみ(直接入力不可)
	g_combo_conf = CreateWindowExW(
		0, WC_COMBOBOXW, L"",
		WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
		0, 0, 0, 0, g_hwnd, (HMENU)IDC_CONF_TARGET, GetModuleHandleW(0), nullptr);
	// 項目の並び順は CONF_MODE_* の値と一致させること
	SendMessageW(g_combo_conf, CB_ADDSTRING, 0,
		(LPARAM)g_config->translate(g_config, L"自動 (優先順位で選択)"));
	SendMessageW(g_combo_conf, CB_ADDSTRING, 0, (LPARAM)L"ProgramData\\aviutl2\\style.conf");
	SendMessageW(g_combo_conf, CB_ADDSTRING, 0, (LPARAM)L"AviUtl2フォルダ\\Data\\style.conf");
	SendMessageW(g_combo_conf, CB_ADDSTRING, 0, (LPARAM)L"AviUtl2フォルダ\\style.conf");
	SendMessageW(g_combo_conf, CB_SETCURSEL, g_conf_mode, 0);

	// 編集対象の style.conf パス表示ラベル
	// SS_PATHELLIPSIS: 幅に収まらないパスを「C:\...\style.conf」の形で省略表示
	g_label_path = CreateWindowExW(
		0, WC_STATICW, L"",
		WS_VISIBLE | WS_CHILD | SS_LEFT | SS_PATHELLIPSIS,
		0, 0, 0, 0, g_hwnd, nullptr, GetModuleHandleW(0), nullptr);

	// 注意書きラベル
	g_label_note = CreateWindowExW(
		0, WC_STATICW,
		g_config->translate(g_config, L"※設定は自動保存されます。タイムラインへの反映には再起動が必要です。"),
		WS_VISIBLE | WS_CHILD | SS_LEFT,
		0, 0, 0, 0, g_hwnd, nullptr, GetModuleHandleW(0), nullptr);

	// 全コントロールに本体テーマのフォントを適用
	const HWND controls[] = {
		g_label_value, g_trackbar, g_btn_small, g_btn_medium,
		g_btn_large, g_btn_apply, g_combo_conf, g_label_path, g_label_note,
	};
	for (HWND c : controls) {
		SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
	}

	UpdateValueLabel();
	UpdatePathLabel();	// 起動時点の編集対象パスを表示

	//------------------------------------------------------------------
	// 本体へウィンドウクライアントとして登録
	// (本体のウィンドウレイアウトにドッキング表示されるようになります)
	//------------------------------------------------------------------
	host->register_window_client(PLUGIN_WINDOW_NAME, g_hwnd);

	// 再起動 API を使う為の編集ハンドルを取得
	// ※EDIT_HANDLE の各機能は RegisterPlugin 内では使用不可の為、取得のみ行います
	g_edit = host->create_edit_handle();
}

//----------------------------------------------------------------------------------
//	DLL エントリポイント
//----------------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		// 設定ファイル(ini)のパス取得に使う為、自身のインスタンスハンドルを保持
		g_hinst = (HINSTANCE)hModule;
		// スレッド毎の DLL_THREAD_ATTACH/DETACH 通知は不要なので無効化 (軽量化)
		DisableThreadLibraryCalls(hModule);
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
