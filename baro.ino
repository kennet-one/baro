// Библиотеки
#include <U8g2lib.h>                                                    // библиотека OLED экрана
#include <Wire.h>                                                       // библиотека интерфейса I2C
#include "ESP32TimerInterrupt.h"
#include <GyverBME280.h>                     
#include "painlessMesh.h"

#define   MESH_PREFIX     "kennet"
#define   MESH_PASSWORD   "kennet123"
#define   MESH_PORT       5555

painlessMesh  mesh;

GyverBME280 bme;                              // Создание обьекта bme

/* Подпрограммы работы с OLED экраном */
U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 5, /* dc=*/ 16, /* reset=*/ 17);

#define PIN_D19             19        // Pin D19 mapped to pin GPIO9 of ESP32

void receivedCallback( uint32_t from, String &msg ) {

  String str1 = msg.c_str();
  String str2 = "bar";

  //if (str1.equals(str2)) {
  //  String x = "09" + String(analogRead(A0)); 
   // mesh.sendSingle(624409705,x);
  //}
}

bool IRAM_ATTR TimerHandler0(void * timerNo)
{
	static bool toggle0 = false;

	digitalWrite(PIN_D19, toggle0);
	toggle0 = !toggle0;

	return true;
}

#define TIMER0_INTERVAL_MS        500
#define TIMER0_DURATION_MS        499

ESP32Timer ITimer0(0);

// Макросы
#define PERIOD          2445                // 2445 - для 3 суток (приблизительно 41 минута)
#define LENGHT_ARRAY    106                 // размер массива
// Если вам нужно давление над уровнем моря, то укажите свою высоту местности и
// в файле measure расскоментируйте строку 25, а 26-ю закоментируйте!
#define ALTITUDE        29                  // высота вашего местоположения в метрах над уровнем моря

// Переменные
char                    p_str[6];
bool                    flagReady = true, flagDown = false, flagErrors;
double                  pressure, Tbmp;
uint8_t                 idx = 0;
uint16_t                amplitudeGraf, arrayPress[LENGHT_ARRAY];
uint16_t                countReady = PERIOD, timeDown;
const uint16_t          baseLine = 740, baseLineGraf = 52;
const uint16_t          rightSideGraf = 105;

// Прототипи функцій
void drawPlane(void);
void drawGraf(void);
void drawDataPress(void);
void drawMessageEpsen(void);
uint16_t determineIndex(int16_t s);

// Рисуем координатную плоскость (время и давление)
void drawPlane(void)
{
  u8g2.setFont(u8g2_font_5x7_tf);                                                         // задаём шрифт
  u8g2.drawHLine(0, 52, 105);                                                             // рисуем шкалу времени
  u8g2.drawVLine(105, 0, 55);                                                             // рисуем шкалу давления
  // Выводим в цикле
  for (uint8_t i = 0; i < 3; i ++)                                                      
  {
    u8g2.setCursor(110, 15 + 20*i);                                                       // указываем положение цифр (740, 760, 780)
    u8g2.print(780 - 20*i);                                                               // выводим цифры
    u8g2.drawHLine(105, 12 + 20*i, 4);                                                    // рисуем горизонтальные чёрточки на шкале давления
    u8g2.drawVLine(35*i, 52, 4);                                                          // рисуем вертикальные чёрточки на шкале времени
    u8g2.setCursor(35*i, 63);                                                             // указываем положение суток (-3день, -2день, -1день)
    u8g2.print(i - 3);                                                                    // выводим день
    u8g2.drawStr(35*i + 10, 63, "d");                                                     // выводим букву d
  }
}

// Определение текущего индекса масива (для графика)
uint16_t determineIndex(int16_t s)
{
  if (s < 0) return (LENGHT_ARRAY + s);
  else return s;
}

// Рисуем график
void drawGraf(void)
{
  for (uint8_t i = 0; i < LENGHT_ARRAY; i ++)                                             // выводим в цикле
  {
    uint8_t point_X = rightSideGraf - i;                                                  // определяем координату Х
    amplitudeGraf =  baseLineGraf - arrayPress[determineIndex(idx - i)];                // определяем координату Y
    // Если в массиве нулевое значение, то график не выводим!
    // Иначе рисуем линию по рассчитаным координатам.
    if (amplitudeGraf != 0) u8g2.drawLine(point_X, baseLineGraf,point_X, amplitudeGraf);  // только если давление выше 740 ммРтСт
    // Нужно ли переводить место вывода текущих показаний вниз?
    // Если в указанной точке (начало) выполняется условие по amplitudeGraf, то взводим флаг и считаем количество выполненных условий
    if (point_X == 55 && amplitudeGraf <= 30)                                             
    {
      flagDown = true;                                                                    // взводим флаг "Опустить вниз!"
      timeDown ++;                                                                        // считаем количество выполненных условий
    }
    // Если в указанной точке (конец) выполняется условие по amplitudeGraf, то вычитаем количество выполненных условий
    if (point_X == 0 && amplitudeGraf <= 30) timeDown --;                                 // вычитаем 
    if(timeDown == 0) flagDown = false;                                                   // если выполняется условие, то сброс флага
  }
}

// Вывод текущего давления
void drawDataPress (void)
{
  u8g2.setFont(u8g2_font_10x20_tf);                                                       // задаём шрифт
  if (flagDown == true)                                                                   // если нужно опустить вниз, то
  {
    u8g2.setColorIndex(0);                                                                // переключаемся на чёрный цвет
    u8g2.drawBox(2, 25, 53, 22);                                                          // рисуем прямоугольник (стираем участок гистограммы)
    u8g2.setColorIndex(1);                                                                // переключаемся на белый цвет
    if (flagErrors == true) u8g2.drawStr(3, 43, "ERROR");                                 // если взведён флаг ошибки, то выводим "ОШИБКА"
     else u8g2.drawStr(3, 43, p_str);                                                     // иначе выводим текущие показания
  }
  else                                                                                    // остаёмся наверху
    {
      if (flagErrors == true) u8g2.drawStr(3, 13, "ERROR");                               // если взведён флаг ошибки, то выводим "ОШИБКА"
       else u8g2.drawStr(3, 13, p_str);                                                   // иначе выводим текущие показания
    }
}

// Вывод сообщения об отсутствии датчика
void drawMessageEpsen(void)
{
  u8g2.setFont(u8g2_font_9x15_tf);                                                        // задаём шрифт
  u8g2.drawStr(40,20,"BMP180");                                                           // выводим сообщение
  u8g2.setFont(u8g2_font_6x13_tf);                                                        // задаём шрифт
  u8g2.drawStr(30,45,"is not found");                                                     // выводим сообщение
}

// Конструктор
// SFE_BMP180 bmp;                                                         // создаём объект SFE_BMP180 и называем его bmp

// Обработка прерывания по таймеру
void timerOneRupt()
{
  flagReady = true;                                                     // взводим флаг готовности
}

void setup()
{
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);

  pinMode(PIN_D19, OUTPUT);
  while (!Serial && millis() < 5000);
  delay(500);

  bme.begin();  
  // Interval in microsecs
	if (ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0))
	{
		Serial.print(F("Starting  ITimer0 OK, millis() = "));
		Serial.println(millis());
	}
	else
		Serial.println(F("Can't set ITimer0. Select another freq. or timer"));

	Serial.flush();
  Serial.begin(9600);                                                 // применялось при отладке программы

  u8g2.begin();                                                         // инициализация экрана

  u8g2.setContrast(0);                                                  // установим нужный контраст
}

void loop()
{
  mesh.update();

  static unsigned long lastTimer0 = 0;
  static bool timer0Stopped = false;

  if (millis() - lastTimer0 > TIMER0_DURATION_MS)
  {
    lastTimer0 = millis();

    if (timer0Stopped)
    {
      timerOneRupt();
      ITimer0.restartTimer();
    }
    else
    {
      ITimer0.stopTimer();
    }

    timer0Stopped = !timer0Stopped;
  }

  if (flagReady == true)                                                // если взведён флаг готовности
  {
    pressure =  bme.readPressure() / 133.322;;
    dtostrf(pressure, 2, 1, p_str);                                   // конвертируем значение давления в строку
    flagErrors = false;                                               // сброс флага ошибки
    

    // Формирование графика (1 раз в 41 минуту)
    if (countReady == PERIOD)                                           // если пришло время
    {
      countReady = 0;                                                   // сброс счётчика
      if (flagErrors == false)                                          // если нет ошибок, то
      {
        if (pressure <= 730.0) arrayPress[idx] = 0;                   // если давление ниже 730 ммРтСт, в массив сохраняем нуль (ограничение по низу)
         else                                                           // иначе
          {
            if (pressure > 792.0) pressure = 791.9;                     // если давление выше 792 ммРтСт, корректируем его (ограничение по верху экрана)
            arrayPress[idx] = (uint16_t)pressure - baseLine;          // запись в массив изменения давления от базовой линии (в px)
          }
      }
      else arrayPress[idx] = 0;                                       // если есть ошибки, то в массив записываем нуль

      if (++idx >= LENGHT_ARRAY) idx = 0;                           // выбор следующей ячейки массива для записи
    }
    else countReady ++;                                                 // пдсчёт времени
    
    // Вывод изображения
    u8g2.firstPage();                                                   // начало вывода
    do
    {
      drawPlane();                                                      // прорисовка шкал (время и давление)
      drawGraf();                                                       // вывод графика
      drawDataPress();                                                  // вывод текущих показаний
    }
    while (u8g2.nextPage());                                            // конец вывода

    flagReady = false;                                                  // сброс флага готовности
  }
}