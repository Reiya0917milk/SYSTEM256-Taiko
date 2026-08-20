# SYSTEM256-Taiko

namcoの太鼓の達人アーケード基板(SYSTEM256)を、[iona-us](https://github.com/toyoshim/iona-us)
（Designed By Mellow PCB）経由でUSBコントローラー（キーボード／ゲームパッド）から
遊べるようにする改造プロジェクトです。

元は`iona-us`（ファームウェア本体）と`IonaFlashServer`（書き込みツール）を別々の
リポジトリとして公開していましたが、`git subtree`でそれぞれのコミット履歴を保った状態で
このリポジトリ1本に統合しました。**現在、単体リポジトリは存在しません。**
このリポジトリだけを見れば全体が揃っています。

## 構成

- [`iona-us/`](iona-us) — 太鼓の達人向けに改造したiona-usファームウェア本体
  （[toyoshim/iona-us](https://github.com/toyoshim/iona-us)のフォーク）
- [`IonaFlashServer/`](IonaFlashServer) — ローカルでファームウェアを書き込むためのC#製ツール

## 経緯・技術メモ

太鼓の達人の入力(KL/DL/DR/KR×1P/2P)がSYSTEM256側でどのアナログチャンネルに
対応するか、実機のTAIKO TESTメニューで総当たりして特定した経緯などは、
[`iona-us/us/controller.c`](iona-us/us/controller.c)冒頭のコメントを参照してください。
