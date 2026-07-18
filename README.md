# LayerHeightSlider for AviUtl ExEdit2

AviUtl ExEdit2 (AviUtl2) のタイムライン（レイヤー編集）の**レイヤー高さを、GUIのスライダーから任意の値に変更できる**汎用プラグインです。

標準ではレイヤー高さは外観設定ファイル `style.conf` を手動編集しないと変更できませんが、本プラグインを使うとファイルを直接編集することなく、スライダー操作だけで設定できます。

※このプラグインは全てClaudeCodeで作成されました。私自身プログラミングの知識は殆どありません。

## 機能

- スライダー（10〜120px）でレイヤー高さを自由に設定
- プリセットボタン（小=18 / 中=26(既定) / 大=36）
- 「適用して再起動」ボタンで、公式APIによる本体再起動までワンクリック
- `style.conf` の `[Layout]` セクションの **`LayerHeight` の値のみ**を書き換え（他の設定・コメント・改行コードはバイト単位で保持）
- 編集対象の `style.conf` をドロップダウンから選択可能（自動＝優先順位／ProgramData版／Data版／本体フォルダ版）。選択は `LayerHeightSlider.ini` に保存され再起動後も維持
- 編集対象の `style.conf` のパスをウィンドウ内に表示（未作成の場合は「(未作成)」表示）
- 設定は `style.conf` に保存されるため、再起動後も維持
- UIの配色・フォントは本体テーマ（`style.conf` の `[Color]` / `[Font]`）に自動追従

## style.conf の検索優先順位

「自動」選択時は、存在するファイルを次の優先順位で編集します。

1. `%ProgramData%\aviutl2\style.conf`
2. `<AviUtl2.exeのあるフォルダ>\Data\style.conf`
3. `<AviUtl2.exeのあるフォルダ>\style.conf`

ドロップダウンで特定のファイルを直接指定することもできます。未作成のファイルを選択した場合は、新規作成してよいか確認ダイアログが表示されます（優先順位の高い場所に最小限のファイルを新規作成すると、優先順位の低い場所にある既存の `style.conf` の設定が使われなくなる場合があるため）。

## インストール

1. [Releases](../../releases) から `LayerHeightSlider.aux2` をダウンロード（または後述の方法でビルド）
2. `C:\ProgramData\aviutl2\Plugin\` にコピー
3. AviUtl2 を起動し、ウィンドウレイアウトの編集で「**レイヤー高さ設定**」ウィンドウを表示

アンインストールは `.aux2` を削除するだけです。

## 使い方

- スライダーを動かすと値が即座に `style.conf` へ自動保存されます
- タイムラインへの反映には本体の再起動が必要です。「**適用して再起動**」ボタンを押すと確認の上で再起動します
  - ※本体にはレイヤー高さを実行中に変更する公開APIが存在しないため、再起動による反映となります

## ビルド方法

必要環境: Visual Studio 2022 以降（「C++によるデスクトップ開発」ワークロード）

```
build.bat
```

をダブルクリックするだけでビルドできます（vcvars64.batを自動検出し、x64のDLLとして `LayerHeightSlider.aux2` を生成します）。

手動でビルドする場合は「x64 Native Tools Command Prompt」で:

```
cl /LD /O1 /W3 /std:c++17 /EHsc /DUNICODE /D_UNICODE /utf-8 LayerHeightSlider.cpp /link /OUT:LayerHeightSlider.aux2
```

## 制限事項

- タイムラインへのリアルタイム反映はできません（`style.conf` は本体起動時のみ読み込まれ、実行中に変更・再読み込みする公開APIが存在しないため）
- 編集対象が本体フォルダ側の `style.conf` で、AviUtl2 が `Program Files` 配下にある場合、書き込みが権限エラーになることがあります（本体のログウィンドウにエラーが出力されます）

## ライセンス

MIT License（[LICENSE](LICENSE) を参照）

同梱のSDKヘッダーファイル（`plugin2.h` / `config2.h` / `logger2.h`）は
[AviUtl ExEdit2 Plugin SDK](https://spring-fragrance.mints.ne.jp/aviutl/)（MIT License, Copyright (c) 2025 Kenkun）の一部です。

## 謝辞

- ＫＥＮくん氏 — AviUtl ExEdit2 本体およびプラグインSDK
