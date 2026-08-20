# SYSTEM256-Taiko

namcoの太鼓の達人アーケード基板(SYSTEM256)を、[iona-us](https://github.com/toyoshim/iona-us)
（Designed By Mellow PCB）経由でUSBコントローラー（キーボード／ゲームパッド）から
遊べるようにする改造プロジェクトのまとめリポジトリです。

`git subtree`で以下2つのリポジトリを、それぞれのコミット履歴を保った状態で
フォルダとして統合しています。

## 構成

- [`iona-us/`](iona-us) — 太鼓の達人向けに改造したiona-usファームウェア本体
  （[toyoshim/iona-us](https://github.com/toyoshim/iona-us)のフォーク、単体リポジトリは
  [Reiya0917milk/iona-us](https://github.com/Reiya0917milk/iona-us)）
- [`IonaFlashServer/`](IonaFlashServer) — ローカルでファームウェアを書き込むためのC#製ツール
  （単体リポジトリは
  [Reiya0917milk/IonaFlashServer](https://github.com/Reiya0917milk/IonaFlashServer)）

単体で使いたい場合や、それぞれの最新コミット履歴を追いたい場合は上記の個別リポジトリを
参照してください。このリポジトリは両方をまとめて見渡すための作業用まとめです。

## 経緯・技術メモ

太鼓の達人の入力(KL/DL/DR/KR×1P/2P)がSYSTEM256側でどのアナログチャンネルに
対応するか、実機のTAIKO TESTメニューで総当たりして特定した経緯などは、
[`iona-us/us/controller.c`](iona-us/us/controller.c)冒頭のコメントを参照してください。
