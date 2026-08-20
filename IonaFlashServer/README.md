# IonaFlashServer

MP07-IONA-US（[iona-us](https://github.com/toyoshim/iona-us)、CH559ベースのJVS I/O基板）に、
namco SYSTEM256の太鼓の達人向けに改造したファームウェアを書き込むためのローカルツールです。

改造版iona-usファームウェアのソースは、このリポジトリの[`../iona-us/`](../iona-us)にあります。

## これは何をするツールか

- iona-usはUSBホストモードで動作中はブラウザと直接通信できないため、
  ファームウェア書き込み・設定変更はブートローダーモード（WebUSB）経由で行う必要があります。
- 公式のブラウザ設定ツールと同じ仕組み（[CH559Flasher.js](https://github.com/toyoshim/CH559Flasher.js)）
  を使い、ローカルで完結する簡易HTTPサーバー(`HttpListener`)としてC#で実装しています。
- ファームウェア本体(`iona.bin`)はexeに埋め込み済みで、ファイル選択なしで書き込めます。
- キー割り当て（キーボード）・ボタン割り当て（ゲームパッド）を、ファームウェア再ビルドなしで
  ブラウザ画面から変更できます（`iona.bin`内にマジックバイト付きで埋め込まれたテーブルを、
  ブラウザ側で直接パッチしてから書き込む方式）。

## 使い方

1. Visual Studioで`IonaFlashServer.sln`を開き、F5で実行（またはコマンドラインで
   `dotnet run --project IonaFlashServer`）
2. ブラウザが自動で`http://localhost:8080/flash_local.html`を開きます
3. 必要ならキー/ボタン割り当てを変更
4. iona-usのSERVICEボタンを押しながらType-A to Type-Aケーブルで1Pポートに接続
5. 「書き込み」ボタンを押す

## ライセンス表記

このツールが埋め込む`iona.bin`は[toyoshim/iona-us](https://github.com/toyoshim/iona-us)
（BSD 3-Clause License）を改造したものです。詳細は[NOTICE.md](NOTICE.md)を参照してください。
