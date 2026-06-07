#pragma once

#include <stddef.h>
#include <stdint.h>

// 倒计时到时提示音的乐谱表。
//
// 无源蜂鸣器由 LEDC PWM 驱动，可以逐个音符改变方波频率，因此能播放真实旋律。
// 每个音符是 {频率Hz, 时长ms}，频率为 0 表示休止（静音那一段时长）。
// 整套播放是非阻塞的，由 main.cpp 里的 MelodyPlayer 在主循环里逐段推进。
//
// 想新增旋律：在下面照格式加一张表，再到 app_config.h 的 BUZZER_ALERT_MELODY
// 增加对应编号即可。

struct BuzzerNote {
  uint16_t freqHz;     // 0 = 休止
  uint16_t durationMs;
};

// 让乐谱可读的音名常量（十二平均律，A4 = 440Hz）。
namespace bm {
constexpr uint16_t REST = 0;

constexpr uint16_t C4 = 262, CS4 = 277, D4 = 294, DS4 = 311, E4 = 330, F4 = 349,
                   FS4 = 370, G4 = 392, GS4 = 415, A4 = 440, AS4 = 466, B4 = 494;
constexpr uint16_t C5 = 523, CS5 = 554, D5 = 587, DS5 = 622, E5 = 659, F5 = 698,
                   FS5 = 740, G5 = 784, GS5 = 831, A5 = 880, AS5 = 932, B5 = 988;
constexpr uint16_t C6 = 1047, CS6 = 1109, D6 = 1175, DS6 = 1245, E6 = 1319,
                   F6 = 1397, FS6 = 1480, G6 = 1568, GS6 = 1661, A6 = 1760,
                   AS6 = 1865, B6 = 1976;
constexpr uint16_t C7 = 2093;
}  // namespace bm

// 1) 超级马里奥「过关」通关小号声（Stage Clear）。
//    经典的上行三连音 + 收尾，听起来就是“赢了/过关”的感觉。
constexpr BuzzerNote kMelodyMarioClear[] = {
    {bm::G4, 130},  {bm::C5, 130},  {bm::E5, 130},  {bm::G5, 130},
    {bm::C6, 130},  {bm::E6, 130},  {bm::G6, 380},  {bm::REST, 30},
    {bm::E6, 360},  {bm::REST, 40},

    {bm::GS4, 130}, {bm::C5, 130},  {bm::DS5, 130}, {bm::GS5, 130},
    {bm::C6, 130},  {bm::DS6, 130}, {bm::GS6, 380}, {bm::REST, 30},
    {bm::DS6, 360}, {bm::REST, 40},

    {bm::AS4, 130}, {bm::D5, 130},  {bm::F5, 130},  {bm::AS5, 130},
    {bm::D6, 130},  {bm::F6, 130},  {bm::AS6, 380}, {bm::REST, 60},

    {bm::AS6, 150}, {bm::REST, 30}, {bm::AS6, 150}, {bm::REST, 30},
    {bm::AS6, 150}, {bm::REST, 30}, {bm::C7, 520},
};

// 2) 最终幻想「胜利」号角（Victory Fanfare 开头）。
//    极具辨识度的“当当当当~ 当— 当— 当——”胜利音。
constexpr BuzzerNote kMelodyFfVictory[] = {
    {bm::C5, 160}, {bm::REST, 20}, {bm::C5, 160}, {bm::REST, 20},
    {bm::C5, 160}, {bm::REST, 20}, {bm::C5, 440},
    {bm::GS4, 440},
    {bm::AS4, 440},
    {bm::C5, 280}, {bm::REST, 100},
    {bm::AS4, 240}, {bm::REST, 60},
    {bm::C5, 600},
};

// 3) 简洁的「升级 / 答对」上行小铃声（Level-Up chime）。
//    短促轻快，适合不想太张扬时使用。
constexpr BuzzerNote kMelodyLevelUp[] = {
    {bm::E5, 110}, {bm::REST, 20}, {bm::G5, 110}, {bm::REST, 20},
    {bm::C6, 110}, {bm::REST, 20}, {bm::E6, 110}, {bm::REST, 20},
    {bm::G6, 360},
};

struct BuzzerMelody {
  const BuzzerNote *notes;
  size_t count;
};

// 与 app_config.h 中 BUZZER_ALERT_MELODY 的编号一一对应。
constexpr BuzzerMelody kBuzzerMelodies[] = {
    {kMelodyMarioClear, sizeof(kMelodyMarioClear) / sizeof(kMelodyMarioClear[0])},
    {kMelodyFfVictory, sizeof(kMelodyFfVictory) / sizeof(kMelodyFfVictory[0])},
    {kMelodyLevelUp, sizeof(kMelodyLevelUp) / sizeof(kMelodyLevelUp[0])},
};
constexpr size_t kBuzzerMelodyCount =
    sizeof(kBuzzerMelodies) / sizeof(kBuzzerMelodies[0]);
