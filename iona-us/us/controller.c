// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "controller.h"

#include "ch559.h"
#include "serial.h"
#include "timer3.h"

#include "settings.h"

// #define _DBG_HID_REPORT_DUMP
// #define _DBG_HID_DECODE_DUMP
// #define _DBG_HUB1_ONLY

static bool test_sw = false;
static bool service_sw = false;
static uint8_t coin_sw[2] = {0, 0};
static uint8_t coin[2] = {0, 0};
static uint8_t mahjong[4] = {0, 0, 0, 0};

static uint8_t digital_map[2][4];

static uint16_t analog[8];
static uint16_t rotary[2];
static uint16_t screen[4];
static uint8_t gear_sequence[2];
static uint8_t gear_updown[2];

enum {
  MODE_NORMAL,
  MODE_MAHJONG,
};
static uint8_t mode = MODE_NORMAL;

// --- SYSTEM256 太鼓の達人 疑似アナログ入力 ---
// 太鼓Assyの入力はSYSTEM256側で全てアナログ信号として扱われる。
// 実機のTAIKO TESTメニュー(取扱説明書)により、1台の太鼓は
// KL(左縁) DL(左面) DR(右面) KR(右縁) の4ゾーン構成と判明している。
//
// 実機のTAIKO TEST画面で総当たりした結果、analog[0..7]と実際のゾーンの
// 対応は下記の通り（1Pポート挿しでanalog[0..3]、2Pポート挿しでanalog[4..7]
// に書き込まれるが、中身は1P/2Pのゾーンが綺麗には分かれておらず混在している）：
//   analog[0]=1P DL  analog[1]=2P KL  analog[2]=2P DL  analog[3]=1P DR
//   analog[4]=1P KR  analog[5]=1P KL  analog[6]=2P KR  analog[7]=2P DR
// そのため「hubで4つブロック分け」ではなく、hubとゾーンの組み合わせごとに
// 個別のインデックスへ書き込む方式にしてある。これなら1P用/2P用どちらの
// 太鼓デバイスも、挿すポート(1P/2P)にかかわらず同じ4キーで正しく動く。
//
// 下記キーコードはUSB HID Keyboard/Keypad Usage ID (Usage Page 0x07)。
// 実際に接続するキーボード/おうち太鼓のレポートに合わせて変更すること。
// デフォルトはD/F/J/K (太鼓シミュレータでよくあるDFJK配置)。
// (一度DR=Lに変更したが、根拠にしたログがPCキーボードを指で押した結果で
// 実機の出力ではなかったため、実機で確認が取れるまでJに戻してある。)
#define TAIKO_KEY_KL 0x07  // D: 縁
#define TAIKO_KEY_DL 0x09  // F: 面
#define TAIKO_KEY_DR 0x0d  // J: 面
#define TAIKO_KEY_KR 0x0e  // K: 縁

// I/O TESTメニュー操作用（SELECT UP/DOWN, ENTER）とコイン投入用のキー。
// SELUP/SELDOWNはJVSの標準的な1P上下スイッチと同一で、通常のコントローラ
// と同じく settings->digital_map（ブラウザの「ボタン配置」）を経由させる。
// ENTER(決定)は実機で総当たりした結果、RIGHTスロット(→キー)で反応することを
// 確認済み。TEST/SERVICEはiona-us基板上の物理ボタンで代替可能なため、
// ここでは扱わない。
#define TAIKO_KEY_SELECT_UP 0x52     // ↑キー
#define TAIKO_KEY_SELECT_DOWN 0x51   // ↓キー
#define TAIKO_KEY_ENTER 0x4f         // →キー: ENTER(決定)として動作確認済み
#define TAIKO_KEY_COIN 0x22          // 5キー（mahjong_updateのコイン割当と揃えた）

// キー割り当てテーブル。ブラウザの書き込みツールが iona.bin のバイト列から
// 直接この並びを見つけて書き換えられるよう、先頭に4バイトの目印(マジック)を
// 置いた1つの配列にまとめてある("TKMP" + 1P用8個 + 2P用8個のUsage ID)。
// 1P/2Pそれぞれ独立して設定できるよう、hubごとに別ブロックを持つ。
// 各ブロックの順序: KL, DL, DR, KR, SELECT_UP, SELECT_DOWN, ENTER, COIN
static const __code uint8_t keymap_with_magic[4 + 16] = {
    'T', 'K', 'M', 'P',
    // 1P (hub 0)
    TAIKO_KEY_KL,        TAIKO_KEY_DL,   TAIKO_KEY_DR,  TAIKO_KEY_KR,
    TAIKO_KEY_SELECT_UP, TAIKO_KEY_SELECT_DOWN, TAIKO_KEY_ENTER, TAIKO_KEY_COIN,
    // 2P (hub 1) 初期値は1Pと同じ
    TAIKO_KEY_KL,        TAIKO_KEY_DL,   TAIKO_KEY_DR,  TAIKO_KEY_KR,
    TAIKO_KEY_SELECT_UP, TAIKO_KEY_SELECT_DOWN, TAIKO_KEY_ENTER, TAIKO_KEY_COIN,
};
#define KEYMAP(hub) (&keymap_with_magic[4 + (hub) * 8])

// ゲームパッド(太鼓フォース、未改造タタコン等)のボタン→太鼓ゾーン割り当て。
// info->button[]のインデックス(0-12、HID_BUTTON_1..META)を格納する。
// キーボードと同じくマジックバイト("GPMP")付きの配列にして、ブラウザ側から
// バイナリを直接パッチできるようにしてある。0xffは「未割り当て」の意味で、
// その場合は当該ゾーンへの書き込みを行わない(既存のアナログ設定等を邪魔しない)。
// 順序: KL, DL, DR, KR (1Pブロック×4, 2Pブロック×4)
// デフォルトはHID_BUTTON_1〜4(ボタン1〜4)を仮に割り当ててあるが、実機の
// ボタン配置は機種依存なので、TAIKO TESTで確認しながら調整すること。
static const __code uint8_t gamepad_keymap_with_magic[4 + 8] = {
    'G', 'P', 'M', 'P',
    0, 1, 2, 3,  // 1P (hub 0): KL, DL, DR, KR
    0, 1, 2, 3,  // 2P (hub 1): KL, DL, DR, KR
};
#define GAMEPAD_KEYMAP(hub) (&gamepad_keymap_with_magic[4 + (hub) * 4])

// --- 太鼓ヒット信号のエッジトリガー化(固定長パルス) ---
// キーボード/ゲームパッドの「今その瞬間押されているか」をそのままanalog[]へ
// 反映すると、連打時に前の打鍵の余韻が次の打鍵と重なって、SYSTEM256側から
// 見て「ずっと押されっぱなしの1打」にしか見えなくなり、2打目以降の
// 立ち上がりエッジが消えてしまうことがある(実機の鬼のカツ連打で確認済み。
// 過去に試した「離してもしばらくONを保持する」延長方式は、この張り付きを
// 悪化させただけだった)。
// 対策として、他のSYSTEM256太鼓プレイ実装(Arduino経由でアナログ対応I/O
// ボードに繋ぐ方式)を参考に、「押された瞬間(立ち上がりエッジ)を検出し、
// キーの押しっぱなし時間に関わらず固定長のパルスを1発だけ出す」方式に
// 変更する。押しっぱなしにしても延長せず、パルスが終われば必ずOFFに戻る
// ので、次の打鍵までの間に必ずOFF区間ができ、SYSTEM256側が別々のヒットと
// して検出しやすくなる。
// パルス幅は実機の反応と連打の潰れ具合を見ながら微調整が要る値なので、
// キー割り当てテーブルと同じくマジックバイト("TPLS")付きにして、
// ブラウザの書き込みツールからビルドし直さずに調整できるようにしてある。
static const __code uint8_t taiko_pulse_msec_with_magic[4 + 1] = {
    'T', 'P', 'L', 'S',
    20,
};
#define TAIKO_HIT_PULSE_MSEC (taiko_pulse_msec_with_magic[4])

// ゾーンごとのパルス状態。インデックスはanalog[]と同じ(0..7)。
static uint16_t hit_pulse_started_at[8];
static bool hit_pulse_active[8];
static bool hit_prev_pressed[8];

// 太鼓の1ゾーン分の入力(press/release)をanalog[index]へ書き込む。
// 立ち上がりエッジを検出したら固定長(TAIKO_HIT_PULSE_MSEC)のパルスを
// 1発出す。パルス中は新たな押下を無視し、パルスが終わったら必ずOFFに戻す
// (押しっぱなしにしてもパルス幅は伸びない)。
static void taiko_hit_zone(uint8_t index, bool pressed) {
  if (hit_pulse_active[index]) {
    if (timer3_tick_msec_between(
            hit_pulse_started_at[index],
            hit_pulse_started_at[index] + TAIKO_HIT_PULSE_MSEC)) {
      analog[index] = 0xffff;  // パルス継続中
      hit_prev_pressed[index] = pressed;
      return;
    }
    hit_pulse_active[index] = false;
    analog[index] = 0x0000;
  }
  if (pressed && !hit_prev_pressed[index]) {
    // 立ち上がりエッジ: 新しいパルスを開始する。
    hit_pulse_active[index] = true;
    hit_pulse_started_at[index] = timer3_tick_msec();
    analog[index] = 0xffff;
  } else {
    analog[index] = 0x0000;
  }
  hit_prev_pressed[index] = pressed;
}

static bool key_pressed(const uint8_t* data, uint8_t keycode) {
  // USBキーボードレポート(8byte): byte0=修飾キー, byte1=予約,
  // byte2-7=最大6キー同時押しのUsage ID(0はキー無し)。
  for (uint8_t i = 2; i < 8; ++i) {
    if (data[i] == keycode) {
      return true;
    }
  }
  return false;
}

// --- USBキーボードのオーバーフロー("Error RollOver")対策 ---
// NKRO非対応のキーボード/おうち太鼓では、同時押しキー数がBootキーボード
// レポートの表現上限(6キー)を超えると、キー欄(byte2-7)が全て
// Usage ID 0x01(Keyboard ErrorRollOver)で埋められる。0x01は通常の押下では
// 絶対に出てこない値なので、これが来た＝「何かが押されているが何が押され
// ているかは分からない」という意味になる。これをそのまま
// key_pressed()に通すと全キー不一致(=全部離された)と誤認し、実際には
// まだ押されているキーまで一瞬OFFになってしまう(太鼓ゾーンの取りこぼしや
// ENTER/SELECTの誤動作につながる)。
// 対策として、オーバーフロー中のレポートは捨て、直前の正常なレポートから
// 得た各キーの状態をそのまま維持する(ヒット信号を人工的に延長するわけでは
// ないので、連打の検出には影響しない)。
static bool last_kl[2], last_dl[2], last_dr[2], last_kr[2];
static bool last_select_up[2], last_select_down[2], last_enter[2];
static bool last_coin_key[2];

static bool keyboard_report_overflowed(const uint8_t* data) {
  return data[2] == 0x01;
}

static bool button_check(uint16_t index, const uint8_t* data) {
  if (index == 0xffff) {
    return false;
  }
  uint8_t byte = index >> 3;
  uint8_t bit = index & 7;
  return data[byte] & (1 << bit);
}

uint16_t analog_check(const struct hid_info* info,
                      const uint8_t* data,
                      uint8_t index,
                      bool polarity) {
  if (info->axis[index] == 0xffff) {
    // return 0x8000;
  } else if (info->axis_size[index] == 8) {
    uint8_t v = data[info->axis[index] >> 3];
    v <<= info->axis_shift[index];
    if (info->axis_sign[index]) {
      v += 0x80;
    }
    if (info->axis_polarity[index] ^ polarity) {
      v = 0xff - v;
    }
    return v << 8;
  } else if (info->axis_size[index] == 10 || info->axis_size[index] == 12) {
    uint8_t byte_index = info->axis[index] >> 3;
    uint16_t l = data[byte_index + 0];
    uint16_t h = data[byte_index + 1];
    uint16_t v = (((h << 8) | l) >> (info->axis[index] & 7))
                 << (16 - info->axis_size[index]);
    v <<= info->axis_shift[index];
    if (info->axis_sign[index]) {
      v += 0x8000;
    }
    if (info->axis_polarity[index] ^ polarity) {
      v = 0xffff - v;
    }
    return v;
  } else if (info->axis_size[index] == 16) {
    uint8_t byte = info->axis[index] >> 3;
    uint16_t v = data[byte] | ((uint16_t)data[byte + 1] << 8);
    v <<= info->axis_shift[index];
    if (info->axis_sign[index]) {
      v += 0x8000;
    }
    if (info->axis_polarity[index] ^ polarity) {
      v = 0xffff - v;
    }
    return v;
  }
  return 0x8000;
}

static void mahjong_update(const uint8_t* data) {
  uint8_t i;
  for (i = 0; i < 4; ++i) {
    mahjong[i] = 0;
  }
  if (data[0] & 0x11) {
    // Ctrl: Kan
    mahjong[0] |= 0x04;
  }
  if (data[0] & 0x22) {
    // Shift: Reach
    mahjong[1] |= 0x04;
  }
  if (data[0] & 0x44) {
    // Alt: Pon
    mahjong[3] |= 0x08;
  }
  bool coin_key = false;
  for (i = 2; i < 8; ++i) {
    if (0x04 <= data[i] && data[i] <= 0x07) {
      // A-D
      mahjong[data[i] - 0x04] |= 0x80;
    } else if (0x08 <= data[i] && data[i] <= 0x0b) {
      // E-H
      mahjong[data[i] - 0x08] |= 0x20;
    } else if (0x0c <= data[i] && data[i] <= 0x0f) {
      // I-L
      mahjong[data[i] - 0x0c] |= 0x10;
    } else if (0x10 <= data[i] && data[i] <= 0x11) {
      // M-N
      mahjong[data[i] - 0x10] |= 0x08;
    } else if (data[i] == 0x2c) {
      // Space: Chi
      mahjong[2] |= 0x08;
    } else if (data[i] == 0x1d) {
      // Z: Ron
      mahjong[2] |= 0x04;
    } else if (data[i] == 0x1e) {
      // 1: Start
      mahjong[0] |= 0x02;
    } else if (data[i] == 0x22) {
      // 5: Coin
      coin_key = true;
    }
  }
  coin_sw[0] = (coin_sw[0] << 1) | (coin_key ? 1 : 0);
  if ((coin_sw[0] & 3) == 1) {
    coin[0]++;
  }
}

static void controller_reset_digital_map(uint8_t player) {
  for (uint8_t i = 0; i < 4; ++i) {
    digital_map[player][i] = 0;
  }
}

static void update_digital_map(uint8_t* dst, uint8_t* src, bool on) {
  if (!on) {
    return;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    dst[i] |= src[i];
  }
}

void controller_reset(void) {
  for (uint8_t p = 0; p < 2; ++p) {
    controller_reset_digital_map(p);
    gear_sequence[p] = 1;
    gear_updown[p] = 0;
    last_kl[p] = false;
    last_dl[p] = false;
    last_dr[p] = false;
    last_kr[p] = false;
    last_select_up[p] = false;
    last_select_down[p] = false;
    last_enter[p] = false;
    last_coin_key[p] = false;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    analog[i] = 0;
    hit_pulse_active[i] = false;
    hit_prev_pressed[i] = false;
  }
  for (uint8_t i = 0; i < 2; ++i) {
    rotary[i] = 0;
  }
  for (uint8_t i = 0; i < 4; ++i) {
    screen[i] = 0;
  }
}

void controller_update(uint8_t hub_index,
                       const struct hid_info* info,
                       const uint8_t* data,
                       uint16_t size) {
#ifdef _DBG_HUB1_ONLY
  const uint8_t hub = 0;
  hub_index;
#else
  const uint8_t hub = hub_index;
#endif
#ifdef _DBG_HID_REPORT_DUMP
  static uint8_t old_data[256];
  bool modified = false;
  for (uint8_t i = 0; i < size; ++i) {
    if (old_data[i] == data[i]) {
      continue;
    }
    modified = true;
    old_data[i] = data[i];
  }
  if (!modified) {
    return;
  }
  Serial.printf("Report %d Bytes: ", size);
  for (uint8_t i = 0; i < size; ++i) {
    Serial.printf("%x,", data[i]);
  }
  Serial.println("");
#endif  // _DBG_HID_REPORT_DUMP
#ifdef _DBG_HID_DECODE_DUMP
  for (uint8_t i = 0; i < 6; ++i) {
    uint16_t value = analog_check(
        info, data, i, settings_get()->analog_polarity[hub][i]);
    Serial.printf("analog %d: %x%x\n", i, value >> 8, value & 0xff);
  }
  Serial.printf("digital: ");
  for (uint8_t i = 0; i < 13; ++i) {
    Serial.printf("%d ", button_check(info->button[i], data) ? 1 : 0);
  }
  Serial.println("");
#endif  // _DBG_HID_DECODE_DUMP
  controller_reset_digital_map(hub);

  if (info->state != HID_STATE_READY) {
    return;
  }

  if (info->type == HID_TYPE_KEYBOARD) {
    if (size == 8) {
      // 実機のTAIKO TESTで確認した実際のインデックス対応（ファイル冒頭コメント参照）。
      // hub 0 = 1Pポート、hub 1 = 2Pポート。
      const __code uint8_t* keymap = KEYMAP(hub);
      bool kl, dl, dr, kr;
      bool select_up, select_down, enter, coin_key;
      if (keyboard_report_overflowed(data)) {
        // オーバーフロー中は直前の状態を維持し、このレポートは捨てる。
        kl = last_kl[hub];
        dl = last_dl[hub];
        dr = last_dr[hub];
        kr = last_kr[hub];
        select_up = last_select_up[hub];
        select_down = last_select_down[hub];
        enter = last_enter[hub];
        coin_key = last_coin_key[hub];
      } else {
        kl = key_pressed(data, keymap[0]);
        dl = key_pressed(data, keymap[1]);
        dr = key_pressed(data, keymap[2]);
        kr = key_pressed(data, keymap[3]);
        select_up = key_pressed(data, keymap[4]);
        select_down = key_pressed(data, keymap[5]);
        enter = key_pressed(data, keymap[6]);
        coin_key = key_pressed(data, keymap[7]);
        last_kl[hub] = kl;
        last_dl[hub] = dl;
        last_dr[hub] = dr;
        last_kr[hub] = kr;
        last_select_up[hub] = select_up;
        last_select_down[hub] = select_down;
        last_enter[hub] = enter;
        last_coin_key[hub] = coin_key;
      }
      if (hub == 0) {
        taiko_hit_zone(5, kl);  // 1P KL
        taiko_hit_zone(0, dl);  // 1P DL
        taiko_hit_zone(3, dr);  // 1P DR
        taiko_hit_zone(4, kr);  // 1P KR
      } else {
        taiko_hit_zone(1, kl);  // 2P KL
        taiko_hit_zone(2, dl);  // 2P DL
        taiko_hit_zone(7, dr);  // 2P DR
        taiko_hit_zone(6, kr);  // 2P KR
      }

      // I/O TESTメニュー操作（SELECT UP/DOWN, ENTER）。
      // 既存のu/d/l/rパイプラインをそのまま使い、実際のJVSビットへの変換は
      // ブラウザの「ボタン配置」設定（settings->digital_map）に委ねる。
      struct settings* settings = settings_get();
      update_digital_map(digital_map[hub], settings->digital_map[hub][0].data,
                          select_up);
      update_digital_map(digital_map[hub], settings->digital_map[hub][1].data,
                          select_down);
      update_digital_map(digital_map[hub], settings->digital_map[hub][3].data,
                          enter);

      // コイン投入（立ち上がりエッジで1枚加算、mahjong_updateと同じ方式）。
      coin_sw[hub] = (coin_sw[hub] << 1) | (coin_key ? 1 : 0);
      if ((coin_sw[hub] & 3) == 1) {
        coin[hub]++;
      }
    }
    return;
  }

  if (info->report_id) {
    if (info->report_id != data[0]) {
      return;
    }
    data++;
  }

  struct settings* settings = settings_get();
  // Analog to Digital pad map from another controller.
  bool u = button_check(info->dpad[0], data);
  bool d = button_check(info->dpad[1], data);
  bool l = button_check(info->dpad[2], data);
  bool r = button_check(info->dpad[3], data);

  uint8_t alt_digital = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    uint8_t type = settings->analog_type[hub][i];
    if (type == AT_NONE) {
      continue;
    }
    uint8_t index = settings->analog_index[hub][i];
    uint16_t value =
        analog_check(info, data, i, settings->analog_polarity[hub][i]);
    switch (type) {
      case AT_DIGITAL:
        switch (index) {
          case 0:
            l |= value < 0x6000;
            r |= value > 0xa000;
            break;
          case 1:
            u |= value < 0x6000;
            d |= value > 0xa000;
            break;
          case 2:
            alt_digital |= (value < 0x6000) ? 4 : (value > 0xa000) ? 8 : 0;
            break;
          case 3:
            alt_digital |= (value < 0x6000) ? 1 : (value > 0xa000) ? 2 : 0;
            break;
        }
        break;
      case AT_ANALOG:
        analog[index] = value;
        break;
      case AT_ROTARY:
        rotary[index] = value;
        break;
      case AT_SCREEN:
        screen[index] = value;
        break;
    }
  }

  // ゲームパッド(太鼓フォース、未改造タタコン等)のボタンによる太鼓ゾーン入力。
  // 0xff(未割り当て)のスロットは何もしない(上のアナログ設定等を邪魔しない)。
  // analog[]への実際の書き込み先はキーボードと同じ対応表(ファイル冒頭コメント参照)。
  {
    const __code uint8_t* gp = GAMEPAD_KEYMAP(hub);
    bool gp_kl = (gp[0] != 0xff) && button_check(info->button[gp[0]], data);
    bool gp_dl = (gp[1] != 0xff) && button_check(info->button[gp[1]], data);
    bool gp_dr = (gp[2] != 0xff) && button_check(info->button[gp[2]], data);
    bool gp_kr = (gp[3] != 0xff) && button_check(info->button[gp[3]], data);
    if (gp[0] != 0xff || gp[1] != 0xff || gp[2] != 0xff || gp[3] != 0xff) {
      if (hub == 0) {
        if (gp[0] != 0xff) taiko_hit_zone(5, gp_kl);  // 1P KL
        if (gp[1] != 0xff) taiko_hit_zone(0, gp_dl);  // 1P DL
        if (gp[2] != 0xff) taiko_hit_zone(3, gp_dr);  // 1P DR
        if (gp[3] != 0xff) taiko_hit_zone(4, gp_kr);  // 1P KR
      } else {
        if (gp[0] != 0xff) taiko_hit_zone(1, gp_kl);  // 2P KL
        if (gp[1] != 0xff) taiko_hit_zone(2, gp_dl);  // 2P DL
        if (gp[2] != 0xff) taiko_hit_zone(7, gp_dr);  // 2P DR
        if (gp[3] != 0xff) taiko_hit_zone(6, gp_kr);  // 2P KR
      }
    }
  }

  if (info->hat != 0xffff) {
    uint8_t byte = info->hat >> 3;
    uint8_t bit = info->hat & 7;
    uint8_t hat = (data[byte] >> bit) & 0xf;
    switch (hat) {
      case 0:
        u |= true;
        break;
      case 1:
        u |= true;
        r |= true;
        break;
      case 2:
        r |= true;
        break;
      case 3:
        r |= true;
        d |= true;
        break;
      case 4:
        d |= true;
        break;
      case 5:
        d |= true;
        l |= true;
        break;
      case 6:
        l |= true;
        break;
      case 7:
        l |= true;
        u |= true;
        break;
    }
  }

  update_digital_map(digital_map[hub], settings->digital_map[hub][0].data, u);
  update_digital_map(digital_map[hub], settings->digital_map[hub][1].data, d);
  update_digital_map(digital_map[hub], settings->digital_map[hub][2].data, l);
  update_digital_map(digital_map[hub], settings->digital_map[hub][3].data, r);
  if (alt_digital) {
    uint8_t alt_hub = (hub + 1) & 1;
    update_digital_map(digital_map[hub], settings->digital_map[alt_hub][0].data,
                       alt_digital & 1);
    update_digital_map(digital_map[hub], settings->digital_map[alt_hub][1].data,
                       alt_digital & 2);
    update_digital_map(digital_map[hub], settings->digital_map[alt_hub][2].data,
                       alt_digital & 4);
    update_digital_map(digital_map[hub], settings->digital_map[alt_hub][3].data,
                       alt_digital & 8);
  }
  for (uint8_t i = 0; i < 12; ++i) {
    uint8_t rapid_fire = settings->rapid_fire[hub][i];
    update_digital_map(digital_map[hub], settings->digital_map[hub][4 + i].data,
                       (settings->sequence[rapid_fire].on &&
                        button_check(info->button[i], data)) ^
                           settings->sequence[rapid_fire].invert);
  }
  if (settings->gear_sequence_support[hub]) {
    uint8_t current_gear_updown = 0;
    for (uint8_t i = 0; i < 12; ++i) {
      if (settings->gear_control[hub][i] == 0) {
        continue;
      }
      if (button_check(info->button[i], data)) {
        current_gear_updown = settings->gear_control[hub][i];
      }
    }
    if (gear_updown[hub] != current_gear_updown) {
      gear_updown[hub] = current_gear_updown;
      if (current_gear_updown == 1) {
        if (gear_sequence[hub] < 6) {
          gear_sequence[hub]++;
        }
      } else if (current_gear_updown == 2) {
        if (gear_sequence[hub] > 0) {
          gear_sequence[hub]--;
        }
      }
    }
    switch (gear_sequence[hub]) {
      case 0:                         // 0000_0000 [N]
        break;
      case 1:
        digital_map[hub][1] |= 0xa0;  // 1010_0000 [1]
        break;
      case 2:
        digital_map[hub][1] |= 0x60;  // 0110_0000 [2]
        break;
      case 3:
        digital_map[hub][1] |= 0x80;  // 1000_0000 [3]
        break;
      case 4:
        digital_map[hub][1] |= 0x40;  // 0100_0000 [4]
        break;
      case 5:
        digital_map[hub][1] |= 0x90;  // 1001_0000 [5]
        break;
      case 6:
        digital_map[hub][1] |= 0x50;  // 0101_0000 [6]
        break;
    }
  }

  coin_sw[hub] = (coin_sw[hub] << 1) | ((digital_map[hub][0] >> 6) & 1);
  if ((coin_sw[hub] & 3) == 1) {
    coin[hub]++;
  }
}

void controller_poll(void) {
  service_sw = settings_service_pressed();
  test_sw = settings_test_pressed();
}

uint8_t controller_head(void) {
  return test_sw ? 0x80 : 0;
}

uint8_t controller_data(uint8_t player, uint8_t index, uint8_t gpout) {
  if (mode == MODE_MAHJONG) {
    uint8_t service = service_sw ? 0x40 : 0;
    if (gpout == 0x40)
      return mahjong[0] | service;
    if (gpout == 0x20)
      return mahjong[1] | service;
    if (gpout == 0x10)
      return mahjong[2] | service;
    if (gpout == 0x80 || gpout == 0x08)
      return mahjong[3] | service;
    return service;
  }
  uint8_t line = (player << 1) + index;
  if (line >= 4) {
    return 0;
  }
  uint8_t data = digital_map[0][line] | digital_map[1][line];
  if (!line) {
    data &= ~0x40;
    if (!player && service_sw) {
      data |= 0x40;
    }
  }
  return data;
}

uint8_t controller_coin(uint8_t player) {
  return coin[player];
}

uint16_t controller_analog(uint8_t index) {
  if (index < 8) {
    return analog[index];
  }
  return 0x8000;
}

uint16_t controller_rotary(uint8_t index) {
  if (index < 2) {
    return rotary[index];
  }
  return 0x8000;
}

uint16_t controller_screen(uint8_t index, uint8_t axis) {
  uint8_t screen_index = (index << 1) + axis;
  if (screen_index < 4) {
    return screen[screen_index];
  }
  return 0x8000;
}

void controller_coin_add(uint8_t player, uint8_t add) {
  coin[player] += add;
}

void controller_coin_sub(uint8_t player, uint8_t sub) {
  coin[player] -= sub;
}

void controller_coin_set(uint8_t player, uint8_t value) {
  coin[player] = value;
}