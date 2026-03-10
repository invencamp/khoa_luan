#include "stm32l073xx.h"
#include "math.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
uint32_t SystemCoreClock = 2097152U;
void SystemInit (void){}
volatile uint32_t uwTick;
	
	// Bi?n toàn c?c luu tr? giá tr?
volatile int16_t encoder_count = 0;
volatile int16_t encoder_count_prev = 0;
volatile int16_t speed_pulses_per_10ms = 0;
volatile float speed_rpm = 0.0f;

// Thay d?i theo d? phân gi?i th?c t? c?a encoder b?n dang dùng
#define PULSES_PER_REVOLUTION 1320.0f

void Encoder_Init(void) {
    // 1. B?t Clock cho GPIOA và TIM2
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // 2. C?u hình PA0 và PA1 sang ch? d? Alternate Function (AF)
    // Xóa bit c?u hình cu và ghi giá tr? 10 (AF mode) vào MODER
    GPIOA->MODER &= ~(GPIO_MODER_MODE0_Msk | GPIO_MODER_MODE1_Msk);
    GPIOA->MODER |= (2U << GPIO_MODER_MODE0_Pos) | (2U << GPIO_MODER_MODE1_Pos); 

    // 3. Ch?n Alternate Function s? 2 (AF2) cho PA0 và PA1
    // Trên L073, AF2 c?a PA0/PA1 chính là TIM2_CH1 và TIM2_CH2
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL0_Msk | GPIO_AFRL_AFSEL1_Msk);
    GPIOA->AFR[0] |= (2U << GPIO_AFRL_AFSEL0_Pos) | (2U << GPIO_AFRL_AFSEL1_Pos);

    // Kích ho?t di?n tr? kéo lên (Pull-up) d? phòng encoder là lo?i Open-Collector
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0_Msk | GPIO_PUPDR_PUPD1_Msk);
    GPIOA->PUPDR |= (1U << GPIO_PUPDR_PUPD0_Pos) | (1U << GPIO_PUPDR_PUPD1_Pos);

    // 4. C?u hình thanh ghi TIM2 sang ch? d? Hardware Encoder
    // Ánh x? Kênh 1 vào TI1, Kênh 2 vào TI2
    TIM2->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0; 
    
    // Tùy ch?n: B?t b? l?c nhi?u s? (Filter) m?c 3 d? l?c nhi?u t? d?ng co
    TIM2->CCMR1 |= (3U << TIM_CCMR1_IC1F_Pos) | (3U << TIM_CCMR1_IC2F_Pos);

    // Không d?o c?c (b?t c?nh lên)
    TIM2->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P); 

    // Ch?n ch? d? Encoder Mode 3 (Ð?m c? c?nh lên và xu?ng c?a c? 2 kênh A, B)
    TIM2->SMCR &= ~TIM_SMCR_SMS_Msk;
    TIM2->SMCR |= (3U << TIM_SMCR_SMS_Pos); 

    // B?t Counter (B?t d?u d?m)
    TIM2->CR1 |= TIM_CR1_CEN;
}

void SysTick_Init(void) {
    // C?u hình SysTick t?o ng?t m?i 10ms (100Hz)
    // Hàm SysTick_Config du?c ARM cung c?p s?n trong core_cm0plus.h
    // Luu ý: SystemCoreClock ph?i ch?a t?n s? xung nh?p hi?n t?i c?a h? th?ng.
    SysTick_Config(SystemCoreClock / 100);
}

//Timer
void TIM2_us_init(void){
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	TIM2->CR1 = 0;
	TIM2->PSC = (SystemCoreClock / 1000000UL) - 1;
	TIM2->ARR = 0xFFFF;
	TIM2->EGR = TIM_EGR_UG;
	TIM2->CR1 |= TIM_CR1_CEN;
}
void TIM6_Init(void){
	RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
	TIM6->PSC = (SystemCoreClock / 1000000U) - 1;
	TIM6->ARR = 0xFFFF;
	TIM6->CR1 |= TIM_CR1_CEN;
}
void delay_us(uint16_t us){
	TIM6->CNT = 0;
	while (TIM6->CNT < us);
}

#define ENCODER_PORT GPIOA
#define ENCODER_PIN (0x0002U)

void ADC1_Init(void){
	RCC->IOPENR |= (1<<0);
	RCC->APB2ENR |= (1 << 9);
	RCC->CR |= RCC_CR_HSION;
	while (!(RCC->CR & RCC_CR_HSIRDY));
	GPIOA->MODER |= (0b11 << (0 * 2));
	GPIOA->PUPDR &= ~(0b11 << (0 * 2));
	if (ADC1->CR & ADC_CR_ADEN){
		ADC1->CR |= ADC_CR_ADDIS;
		while (ADC1->CR & ADC_CR_ADEN);
	}
	ADC1->CR |= ADC_CR_ADCAL;
	while (ADC1->CR & ADC_CR_ADCAL);
	ADC1->CR |= ADC_CR_ADEN;
	while (!(ADC1->ISR & ADC_ISR_ADRDY));
	ADC1->CHSELR = ADC_CHSELR_CHSEL0;
	ADC1->SMPR |= ADC_SMPR_SMP_2;
}
uint16_t ADC1_Read(void){
	ADC1->CR |= ADC_CR_ADSTART;
	while (!(ADC1->ISR & ADC_ISR_EOC));
	return (uint16_t)ADC1->DR;
}
float adc_filtered = 0.0f;
uint8_t adc_initialized = 0;
uint16_t ADC1_Read_EMA(void){
	uint16_t raw = ADC1_Read();
	if(!adc_initialized){
		adc_filtered = (float)raw;
		adc_initialized = 1;
		return raw;
	}
	float alpha = 0.2f;
	adc_filtered = alpha * raw + (1.0f - alpha) * adc_filtered;
	return (uint16_t)adc_filtered;
}
	
void PWM_Init(void) {
    // 1. B?t xung nh?p (Clock) cho GPIOA và TIM3
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // 2. C?u hình chân PA6 sang ch? d? Alternate Function (AF)
    GPIOA->MODER &= ~GPIO_MODER_MODE6_Msk;                   // Xóa tr?ng thái cu
    GPIOA->MODER |= (2U << GPIO_MODER_MODE6_Pos);            // Ch?n AF mode (10)

    // 3. Ánh x? PA6 vào TIM3_CH1 (AF2 trên dòng STM32L0)
    GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL6_Msk;                  // Xóa AF cu
    GPIOA->AFR[0] |= (2U << GPIO_AFRL_AFSEL6_Pos);           // Ch?n AF2

    // 4. C?u hình Timer 3 d? bam xung PWM (T?n s? 1kHz)
    TIM3->PSC = (SystemCoreClock / 1000000U) - 1;            // B? chia d? Timer d?m m?i 1 microgiây
    TIM3->ARR = 1000 - 1;                                    // Chu k? 1000 us = 1 ms (Tuong duong 1kHz)
    TIM3->CCR1 = 0;                                          // Kh?i t?o Duty Cycle = 0% (Ð?ng co d?ng)

    // 5. C?u hình Kênh 1 (CH1) ho?t d?ng ? ch? d? PWM Mode 1
    TIM3->CCMR1 &= ~TIM_CCMR1_OC1M_Msk;                      // Xóa c?u hình Output Compare cu
    TIM3->CCMR1 |= (6U << TIM_CCMR1_OC1M_Pos);               // 110 (6): PWM mode 1
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE;                          // B?t tính nang Preload cho thanh ghi CCR1

    // 6. Kích ho?t ngõ ra CH1 và cho phép Timer ho?t d?ng
    TIM3->CCER |= TIM_CCER_CC1E;                             // B?t Output trên CH1
    TIM3->CR1 |= TIM_CR1_ARPE;                               // B?t Auto-reload preload
    TIM3->EGR |= TIM_EGR_UG;                                 // C?p nh?t cu?ng b?c các thanh ghi
    TIM3->CR1 |= TIM_CR1_CEN;                                // B?t d?u d?m!
}
typedef struct {
	float setpoint;
	float gain;
	float ki;
	float integral;
	float derivative;
	float dead_zone;
	float output_min;
	float output_max;
} PID_Controller;
void PID_Init(PID_Controller *ctrl, float gain, float dead_zone, float out_min, float out_max){
	ctrl->gain = gain;
	ctrl->integral = 0.0f;
	ctrl->dead_zone = dead_zone;
	ctrl->output_min = out_min;
	ctrl->output_max = out_max;
}
float PID_Compute(PID_Controller *ctrl, float measured_value){
	float error = ctrl->setpoint - measured_value;
	if (fabs(error) < ctrl->dead_zone){
		error = 0.0f;
	}
	ctrl->integral += error * ctrl->ki;
	float max_integral = (ctrl->output_max - ctrl->output_min) / 2.0f;
	if (ctrl->integral > max_integral) ctrl->integral = max_integral;
	if (ctrl->integral < -max_integral) ctrl->integral = -max_integral;
	float output = ctrl->gain * error + ctrl->integral + ctrl->derivative;
	if (output > ctrl->output_max) output = ctrl->output_max;
	if (output < ctrl->output_min) output = ctrl->output_min;
	return output;
}


PID_Controller motor;
float h = 0.01; //Chu ky lay mau
float Speed = 0.0;
float Setpoint = 100.0;
float alpha_encoder = 0.2f; 
float speed_rpm_filtered = 0.0f;

// ---------------------------------------------------------
// Trình ph?c v? ng?t ng?m d?nh k? c?a h? th?ng (M?i 10ms)
// ---------------------------------------------------------
void SysTick_Handler(void) {
    // Tang bi?n th?i gian lên 10ms m?i khi vào ng?t
    uwTick += 10; 

    encoder_count = (int16_t)TIM2->CNT; 
    speed_pulses_per_10ms = (int16_t)(encoder_count - encoder_count_prev);
    encoder_count_prev = encoder_count;

    // 1. Tính t?c d? thô (b? gi?t b?c thang 4.54 RPM)
    speed_rpm = ((float)speed_pulses_per_10ms * 100.0f * 60.0f) / PULSES_PER_REVOLUTION;
    
    // 2. Áp d?ng thu?t toán L?c thông th?p EMA
    // Công th?c: Giá tr? m?i = (Alpha * Raw) + ((1 - Alpha) * Giá tr? cu)
    speed_rpm_filtered = (alpha_encoder * speed_rpm) + ((1.0f - alpha_encoder) * speed_rpm_filtered);
    
    // 3. Gán t?c d? ÐÃ L?C cho h? th?ng PID và Relay s? d?ng
    Speed = speed_rpm_filtered; 
}

	// 2. Bi?n cho Ro-le (Auto-tuning)
float d = 100.0;      // Biên do Ro-le (bam xung PWM 50%) xuât 50% công suât PWM (PWM chay tu 0 - 100 thi xuât muc 50)
float hys = 5.0;     // Hysteresis (Vùng tr? ch?ng nhi?u: 5 vòng/phút)
float a = 0.0;       // Biên d? dao d?ng t?c d? do du?c (S? du?c ph?n m?m t? do)
float Tu = 0.0;      // Chu k? dao d?ng (S? du?c ph?n m?m t? do)
float Ku = 0.0;      // H? s? t?i h?n

// 3. Bi?n cho PID V?n t?c (Velocity Algorithm)
float Kp, Ti, Td;    // Thông s? PID th?i gian
float q0, q1, q2;    // Các h? s? g?p c?a thu?t toán v?n t?c
float e0 = 0, e1 = 0, e2 = 0; // Sai s?: Hi?n t?i, tru?c dó, tru?c dó n?a
float u = 0, u_prev = 0;      // Tín hi?u di?u khi?n (PWM)

// Thêm các bi?n này ? Giai do?n 1
uint32_t t_start = 0;       // Luu th?i di?m b?t d?u do (mili-giây)
int crossing_count = 0;     // Bi?n d?m s? l?n c?t ngang Setpoint
float speed_max = 0.0;      // Luu d?nh trên
float speed_min = 9999.0;   // Luu dáy du?i (kh?i t?o s? r?t l?n)
float error_prev = 0.0;     // Luu sai s? ? chu k? tru?c
bool tuning_done = false;   // C? báo hi?u: Ðã ch?y xong Ro-le chua? (M?c d?nh: Chua)
bool is_calculated = false; // C? báo hi?u: Ðã tính xong Kp, Ti, Td chua? (M?c d?nh: Chua)

void Relay_AutoTune();
void Calculate_PID_ZieglerNichols();
void PID_Velocity_Control();
float Ki = 0.0;
float Kd = 0.0;
char text[32];
char t[32];

int main(void){
//	TIM2_us_init();
//	TIM6_Init();
	SysTick->LOAD = (SystemCoreClock / 1000) - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = 0b111;
	
	RCC->IOPENR |=1;
	RCC->APB1ENR |= (1 << 17); 
	GPIOA->MODER &= ~((0b11 << (2*2))|(0b11 << (3*2)));
	GPIOA->MODER |= (0b10 << (2*2))|(0b10 <<(3*2));
GPIOA->AFR[0] |= (4 << (2 * 4)) | (4 << (3 * 4));
USART2->BRR = SystemCoreClock / 9600;
USART2->CR1 = (1<<3) | (1<<2) | 1 | 1<<5;
NVIC->ISER[0] = (1 << USART2_IRQn); // Kích ho?t ng?t USART2

	ADC1_Init();
	PWM_Init();
	//PID_Init(&motor, 0.002f, 0.0f, 0.0f, 1000.0f);

    // Kh?i t?o ngo?i vi
    Encoder_Init();
    SysTick_Init();
		
	uint32_t last = 0;
	while (1){
//		uint16_t adc_val = ADC1_Read_EMA();
		if (tuning_done == false) {
    Relay_AutoTune(); // C? ch?y Ro-le và t? do di...
		} 
		else {
    // V?a do xong Tu và a, ch? tính toán b? Kp, Ti, Td ÐÚNG 1 L?N!
    if (is_calculated == false) {
        Calculate_PID_ZieglerNichols();
        is_calculated = true;
    }
    
    // T? nay v? sau chuy?n sang ch?y mu?t mà
    PID_Velocity_Control(); 
	}
//		
//		u = 999;
//		TIM3->CCR1 = u;
	if (uwTick - last >= 500){
        // Tr?ng thái 1: Ðang dò thông s? (Auto-Tuning)
        if (tuning_done == false) {
            sprintf(text, "TUNING... enc=%.1f; pwm=%.1f\r\n", Speed, u);
            for (int i = 0; i < strlen(text); i++) {
                USART2->TDR = text[i];
                while (!(USART2->ISR & (1 << 7)));
            }
        } 
        // Tr?ng thái 2: Dò xong, dang ch?y PID
        else {
            sprintf(text, "PID RUN: enc=%.1f; pwm=%.1f\r\n", Speed, u);
            // S?a l?i ch? in cho dúng Kp, Ki, Kd (code cu b?n gõ 2 l?n ch? Kd)
            sprintf(t, "kp=%.1f; ki=%.1f; kd=%.1f\r\n", Kp, Ki, Kd); 
            
            for (int i = 0; i < strlen(text); i++) {
                USART2->TDR = text[i];
                while (!(USART2->ISR & (1 << 7)));
            }
            while (!(USART2->ISR & (1 << 6)));
            
            for (int j = 0; j < strlen(t); j++) {
                USART2->TDR = t[j];
                while (!(USART2->ISR & (1 <<7)));
            }
        }
        
        while (!(USART2->ISR & (1 << 6)));
        last = uwTick;
    }
	
}
	}

void Relay_AutoTune() {
    float error = Setpoint - Speed;
    
    // 1. THU?T TOÁN RO-LE (Bom/Ng?t ga)
    if (error > hys) {
        u = d;       
    } else if (error < -hys) {
        u = -d;      
    }

    // 2. THU?T TOÁN ÐO BIÊN Ð? (a)
    // Liên t?c c?p nh?t d?nh cao nh?t và dáy th?p nh?t
    if (Speed > speed_max) speed_max = Speed;
    if (Speed < speed_min) speed_min = Speed;

    // 3. THU?T TOÁN DÒ ÐI?M C?T & ÐO CHU K? (Tu)
    // Ki?m tra xem t?c d? có v?a c?t ngang Setpoint theo chi?u ÐI LÊN không?
    // (T?c là error t? giá tr? DUONG chuy?n sang giá tr? ÂM ho?c 0)
    if (error_prev > 0 && error <= 0) { 
        crossing_count++; // Tang bi?n d?m di?m c?t

        if (crossing_count == 1) {
            // C?T L?N 1: B?t d?u b?m gi?
            t_start = uwTick; // Hàm l?y th?i gian hi?n t?i c?a STM32 (ho?c millis() c?a Arduino)
            
            // Reset l?i d?nh/dáy d? do chính xác trong chu k? này
            speed_max = Setpoint; 
            speed_min = Setpoint;
        }
        else if (crossing_count == 3) {
            // C?T L?N 3: Ðã di du?c 1 vòng tròn tr?n v?n (lên -> xu?ng -> lên)
            uint32_t t_end = uwTick;
            
            // Tính Tu (Ð?i t? mili-giây sang giây)
            Tu = (t_end - t_start) / 1000.0; 
            
            // Tính biên d? a = (Ð?nh - Ðáy) / 2
            a = (speed_max - speed_min) / 2.0;
            
            // B?t c? báo hi?u dã Tuning xong!
            tuning_done = true; 
        }
    }

    // Luu l?i sai s? cho chu k? ng?t ti?p theo
    error_prev = error;
    
    // Xu?t PWM ra d?ng co (t?m th?i xu?t l?nh Ro-le)
    //TIM3->CCR1 = u;
		// 4. XU?T PWM CHO Ð?NG CO 1 CHI?U
    if (u > 0.0) {
        TIM3->CCR1 = (uint32_t)(u * 10.0); // Nhân 10 d? quy d?i 100% thành 1000
    } else {
        TIM3->CCR1 = 0; // Khi u âm (mu?n lùi) -> Ép v? 0 d? ng?t di?n
    }
}

void Calculate_PID_ZieglerNichols() {
    Ku = (4.0 * d) / (3.14159 * a);

    Kp = 0.6 * Ku;
    Ti = 0.5 * Tu;
    //Td = 0.125 * Tu;

    Ki = Kp / Ti;
    //Kd = Kp * Td;
	
	// B?N PH?I GÁN B?NG 0 ? 2 DÒNG NÀY:
    Td = 0.0;       // T?t hoàn toàn nhi?u t? khâu Ð?o hàm
    Kd = 0.0;

    q0 = Kp + Ki * h + (Kd / h);
    q1 = -(Kp + 2.0 * (Kd / h));
    q2 = Kd / h;
}

void PID_Velocity_Control() {
    e0 = Setpoint - Speed;
    float delta_u = q0 * e0 + q1 * e1 + q2 * e2;

    // 3. Công dôn ra tín hiêu diêu khiên thuc tê
    u = u_prev + delta_u;

    // 4. CH?NG BÃO HÒA (Anti-Windup) b?ng cách c?t ng?n
    // Ngan không cho 'u' vu?t ngu?ng, nh? dó khâu tích phân không b? c?ng d?n vô t?n
    if (u > 100.0) u = 100.0; // Gi?i h?n max PWM = 100%
    if (u < 0.0)   u = 0.0;   // Gi?i h?n min PWM = 0%

    // 5. C?p nh?t bi?n tr?ng thái cho chu k? ti?p theo
    e2 = e1;
    e1 = e0;
    u_prev = u;

    // 6. Xu?t giá tr? 'u' ra thanh ghi bam xung (PWM) c?a d?ng co
    //TIM3->CCR1 = u;
		TIM3->CCR1 = (uint32_t)(u * 10.0); // T? l? 100% -> 1000
}
