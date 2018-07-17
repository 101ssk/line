#include "h8-3069-iodef.h"
#include "h8-3069-int.h"

#include "lcd.h"
#include "ad.h"
#include "timer.h"
//#include "loader.h"
//#include "sci2.h"
//#include "random.h"

// タイマー割り込み間隔
#define TIMER0 1000

#define LSWITCHVAL 190
#define RSWITCHVAL 190

/* 割り込み処理で各処理を行う頻度を決める定数 */
#define DISPTIME 100
#define KEYTIME 1
#define ADTIME  2
#define CONTROLTIME 10

// 左のモーターの動きを表す定数
#define LMSTOP 0x00 //ストップ
#define LMCW 0x01 //正回転
#define LMCCW 0x02 //逆回転
#define LMBLAKE 0x03 //ブレーキ

// 右のモーターの動きを表す定数
#define RMSTOP 0x00 //ストップ
#define RMCW 0x04 //正回転
#define RMCCW 0x08 //逆回転
#define RMBLAKE 0x0c //ブレーキ

/* LCD表示関連 */
/* 1段に表示できる文字数 */
#define LCDDISPSIZE 8

/* A/D変換関連 */
/* A/D変換のチャネル数とバッファサイズ */
#define ADCHNUM   4
#define ADBUFSIZE 8
/* 平均化するときのデータ個数 */
#define ADAVRNUM 4
/* チャネル指定エラー時に返す値 */
#define ADCHNONE -1

/* 割り込み処理に必要な変数は大域変数にとる */
volatile int disp_time, key_time, ad_time,  control_time;

// 割り込み用
volatile int disp_time,disp_flag;

unsigned char rightval, leftval;

// AD変換用
volatile unsigned char adbuf[ADCHNUM][ADBUFSIZE];
volatile int adbufdp;
volatile int ad_val_hold;

// LCD表示文字列
volatile char lcd_str_upper[LCDDISPSIZE+1];
volatile char lcd_str_lower[LCDDISPSIZE+1];

int main();
void int_imia0();
void disp();
int  ad_read(int ch);
void control_proc(void);
void set_str(void);

int main(void){
  int i;

  // 初期化
  ROMEMU();
  // ポートの初期化
  PBDDR = 0x0f;
  P6DDR = 0x01;
  // 表示関係の初期化
  disp_time = 0;
  disp_flag = 1;
  // AD変換初期化
  ad_time = 0;
  adbufdp = 0;
  //　制御初期化
  control_time = 0;
  // 初期化
  lcd_init();
  ad_init();
  timer_init();
  timer_set(0,TIMER0);
  timer_start(0);
  ENINT();

  init_disp_str();

  // ボタンが押されるまでの処理
  while(1){
    // ボタンが押されたらループを抜ける
    if(P6DR & 0x01 == 0){
      break;
    }
  }

  // メインループ
  // 基本的な処理は割り込みハンドラ内に記述する
  while(1){
    if(disp_flag == 1){
      disp();
      disp_flag = 0;
    }
  }
}

#pragma interrupt
void int_imia0(void){

  // LCD表示
  disp_time++;
  if(disp_time >= DISPTIME){
    disp_flag = 1;
    disp_time = 0;
  }

  // AD変換
  ad_time++;
  if(ad_time >= ADTIME){
    ad_scan(0,1);
    ad_time = 0;
  }

  // 制御処理
  control_time++;
  if(control_time >= CONTROLTIME){
    control_proc();
    control_time = 0;
  }

  timer_intflag_reset(0);
  ENINT();
}

#pragma interrupt
void int_adi(void)
     /* A/D変換終了の割り込みハンドラ                               */
     /* 関数の名前はリンカスクリプトで固定している                   */
     /* 関数の直前に割り込みハンドラ指定の #pragma interrupt が必要  */
{
  ad_stop();    /* A/D変換の停止と変換終了フラグのクリア */

  /* ここでバッファポインタの更新を行う */
  /* 　但し、バッファの境界に注意して更新すること */
  if(adbufdp < ADBUFSIZE-1){
  	adbufdp++;
  }else{
  	adbufdp = 0;
  }
  /* ここでバッファにA/Dの各チャネルの変換データを入れる */
  /* スキャングループ 0 を指定した場合は */
  /*   A/D ch0〜3 (信号線ではAN0〜3)の値が ADDRAH〜ADDRDH に格納される */
  /* スキャングループ 1 を指定した場合は */
  /*   A/D ch4〜7 (信号線ではAN4〜7)の値が ADDRAH〜ADDRDH に格納される */
  adbuf[0][adbufdp] = ADDRAH;
  adbuf[1][adbufdp] = ADDRBH;
  adbuf[2][adbufdp] = ADDRCH;
  adbuf[3][adbufdp] = ADDRDH;

  ENINT();      /* 割り込みの許可 */
}

int ad_read(int ch)
     /* A/Dチャネル番号を引数で与えると, 指定チャネルの平均化した値を返す関数 */
     /* チャネル番号は，0〜ADCHNUM の範囲 　　　　　　　　　　　             */
     /* 戻り値は, 指定チャネルの平均化した値 (チャネル指定エラー時はADCHNONE) */
{
  int i,ad,bp;

  if ((ch > ADCHNUM) || (ch < 0)) ad = ADCHNONE; /* チャネル範囲のチェック */
  else {

    /* ここで指定チャネルのデータをバッファからADAVRNUM個取り出して平均する */
    /* データを取り出すときに、バッファの境界に注意すること */
    /* 平均した値が戻り値となる */
    bp = adbufdp;
    ad = 0;

    for(i = 0;i < ADAVRNUM;i++){
      if(bp <= 0){
        bp = ADBUFSIZE-1;
      }else{
        bp--;
      }
      ad += adbuf[ch][bp];
    }
    ad /= ADAVRNUM;
  }
  return ad; /* データの平均値を返す */
}

void control_proc(void)
     /* 制御を行う関数                                           */
     /* この関数はタイマ割り込み0の割り込みハンドラから呼び出される */
{
  // AN1,AN2からの読み込み
  rightval = ad_read(1);
  leftval = ad_read(2);

  // モーターの制御
  PBDR = LMSTOP | RMSTOP;
  PBDR = LMCW | RMCW;

  if(rightval >= RSWITCHVAL){
    
  }
  if(leftval >= LSWITCHVAL){
    
  }
}

void set_str(void){
  int i;

  if(rightval / 100 != 0){
    lcd_str_upper[i++] = (rightval / 100) + '0';
    lcd_str_upper[i++] = (rightval / 10 % 10) + '0';
  }else if(rightval / 10 != 0){
    lcd_str_upper[i++] = (rightval / 10) + '0';
  }
  lcd_str_upper[i] = rightval % 10 + '0';
  lcd_str_upper[++i] = ' ';


  i = 0;
  if(leftval / 100 != 0){
    lcd_str_lower[i++] = (leftval / 100) + '0';
    lcd_str_lower[i++] = (leftval / 10 % 10) + '0';
  }else if(leftval / 10 != 0){
    lcd_str_lower[i++] = (leftval / 10) + '0';
  }
  lcd_str_lower[i] = leftval % 10 + '0';
  lcd_str_lower[++i] = ' ';
}

void init_disp_str(){
  int i;
  for(i = 0;i < LCDDISPSIZE;i++){
    lcd_str_upper[i] = ' ';
    lcd_str_lower[i] = ' ';
  }
  lcd_str_upper[i] = lcd_str_lower[i] = '\0';
}

void disp(void){
  set_str();
  lcd_cursor(0,0);
  lcd_printstr(lcd_str_upper);
  lcd_cursor(0,1);
  lcd_printstr(lcd_str_lower);
}
