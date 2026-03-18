#include "stm32l073xx.h"
#include "math.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // Thêm thu vi?n này ? d?u file d? dùng hàm atof()

uint32_t SystemCoreClock = 2097152U;
#define PULSES_PER_REVOLUTION 1320.0f

float Setpoint = 80.0;
float alpha_encoder = 0.2f; 
float d = 50.0;      // Biên do Ro-le (bam xung PWM 100%) xuât 100% công suât PWM
float h = 0.01; //Chu ky lay mau

volatile uint32_t uwTick;
volatile uint32_t t_start = 0;       // thoi diêm bat dâu do (ms)
volatile int16_t encoder_count_prev = 0;
volatile float Speed = 0.0;
volatile float Speed_prev1 = 0, Speed_prev2 = 0; 
volatile float a = 0.0;       // Biên dô dao dông tôc dô
volatile float Tu = 0.0;      // Chu ky dao dông
volatile float Kp, Ki, Kd, Ti, Td;
volatile float q0, q1, q2;    
volatile float u = 0, u_prev = 0;      // Tin hiêu diêu khiên (PWM)
volatile float error_prev = 0.0;     // Luu sai sô o chu ky truoc
volatile int crossing_count = 0;     // sô lân cat ngang Setpoint
volatile float speed_max = 0.0;      
volatile float speed_min = 9999.0;   
volatile float e0 = 0, e1 = 0, e2 = 0; // Sai sô: Hiên tai, truoc do, truoc do nua
volatile bool tuning_done = false;   // chay xong Ro-le chua? (Chua)
volatile bool is_calculated = false; // tinh xong Kp, Ti, Td chua? (Chua)

volatile float ad = 0.0, bd = 0.0;
volatile float delta_D_prev = 0.0; 
float N_filter = 10.0; // Tham s? b? l?c (N thu?ng t? 8 d?n 20)

// Khai báo bi?n toàn c?c
volatile char rx_buffer[16];
volatile uint8_t rx_index = 0;
volatile bool new_setpoint_received = false;

void SystemInit (void){}
void Encoder_Init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN; //Bât Clock cho GPIOA
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;//Bât Clock cho TIM2
    //PA0 va PA1 -> Alternate Function (AF)(10)
    GPIOA->MODER &= ~(GPIO_MODER_MODE0_Msk | GPIO_MODER_MODE1_Msk);
    GPIOA->MODER |= (2U << GPIO_MODER_MODE0_Pos) | (2U << GPIO_MODER_MODE1_Pos); 
    //PA0 va PA1 -> Alternate Function sô 2 (AF2)
		// Trên L073:
		//AF2 cua PA0 la TIM2_CH1
		//AF2 cua PA1 la TIM2_CH2
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFSEL0_Msk | GPIO_AFRL_AFSEL1_Msk);
    GPIOA->AFR[0] |= (2U << GPIO_AFRL_AFSEL0_Pos) | (2U << GPIO_AFRL_AFSEL1_Pos);
    // Kich hoat diên tro keo lên (Pull-up) dê phong encoder la loai Open-Collector
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD0_Msk | GPIO_PUPDR_PUPD1_Msk);
    GPIOA->PUPDR |= (1U << GPIO_PUPDR_PUPD0_Pos) | (1U << GPIO_PUPDR_PUPD1_Pos);
    // TIM2 -> Hardware Encoder
    // Anh xa Kênh 1 -> TI1, Kênh 2 -> TI2
    TIM2->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0; 
    // T?y ch?n: Bât bô loc nhiêu sô (Filter) m?c 3 dê loc nhiêu tu dông co
    TIM2->CCMR1 |= (3U << TIM_CCMR1_IC1F_Pos) | (3U << TIM_CCMR1_IC2F_Pos);
    // Kh?ng d?o c?c (bât canh lên)
    TIM2->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P); 
    // Ch?n ch? dô Encoder Mode 3 (??m ca canh lên va xuông cua ca 2 kênh A, B)
    TIM2->SMCR &= ~TIM_SMCR_SMS_Msk;
    TIM2->SMCR |= (3U << TIM_SMCR_SMS_Pos); 
    // Bât Counter (Bat dâu dêm)
    TIM2->CR1 |= TIM_CR1_CEN;
}
	
void PWM_Init(void) {
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN; //Bât xung nhip (Clock) cho GPIOA
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;//Bât xung nhip(Clock) cho TIM3
    // PA6 -> Alternate Function (AF)
    GPIOA->MODER &= ~GPIO_MODER_MODE6_Msk;                   // Xoa trang thai cu
    GPIOA->MODER |= (2U << GPIO_MODER_MODE6_Pos);            // Chon AF mode (10)
    // Anh xa PA6 -> TIM3_CH1 (AF2) (STM32L0)
    GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL6_Msk;                  // Xoa AF cu
    GPIOA->AFR[0] |= (2U << GPIO_AFRL_AFSEL6_Pos);           // Chon AF2
    // Câu hinh Timer 3 dê bam xung PWM (Tân sô 1kHz)
    TIM3->PSC = (SystemCoreClock / 1000000U) - 1;            // Bô chia d? Timer dêm môi 1 ms
    TIM3->ARR = 1000 - 1;                                    // Chu ky 1000 us = 1 ms (Tuong duong 1kHz)
    TIM3->CCR1 = 0;                                          // Khoi tao Duty Cycle = 0%
    //Kênh 1 (CH1) -> PWM Mode 1
    TIM3->CCMR1 &= ~TIM_CCMR1_OC1M_Msk;                      // Xoa câu hinh Output Compare cu
    TIM3->CCMR1 |= (6U << TIM_CCMR1_OC1M_Pos);               // 110 (6): PWM mode 1
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE;                          // Preload -> CCR1
    TIM3->CCER |= TIM_CCER_CC1E;                             // Bât Output trên CH1
    TIM3->CR1 |= TIM_CR1_ARPE;                               // Bât Auto-reload preload
    TIM3->EGR |= TIM_EGR_UG;                                 // Câp nhât cuong buc c?c thanh ghi
    TIM3->CR1 |= TIM_CR1_CEN;                                // Bat dâu dêm! (cho phép Timer hoat dông)
}

void Relay_AutoTune();
void Calculate_PID_ZieglerNichols();
void PID_Velocity_Control();

void SysTick_Handler(void) {//ngat
    uwTick += 10; 
    volatile int16_t encoder_count = TIM2->CNT; 
		volatile int16_t delta_ticks = (int16_t)(encoder_count - encoder_count_prev);
    volatile float speed_rpm = (delta_ticks * 100.0f * 60.0f) / PULSES_PER_REVOLUTION;
    // Loc thông thâp EMA
		Speed = (alpha_encoder * speed_rpm) + ((1.0f - alpha_encoder) * Speed);
		encoder_count_prev = encoder_count;
	
		if (tuning_done == false) {
        Relay_AutoTune();
    } else {
        // Ch? tính thông s? Z-N dúng 1 l?n khi v?a chuy?n mode
        if (is_calculated == false) {
            Calculate_PID_ZieglerNichols();
            is_calculated = true;
        }
        PID_Velocity_Control();
    }
}



int main(void){
	SysTick->LOAD = (SystemCoreClock / 100) - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = 0b111;
	
	RCC->IOPENR |=1;
	RCC->APB1ENR |= (1 << 17); 
	GPIOA->MODER &= ~((0b11 << (2*2))|(0b11 << (3*2)));
	GPIOA->MODER |= (0b10 << (2*2))|(0b10 <<(3*2));
	GPIOA->AFR[0] |= (4 << (2 * 4)) | (4 << (3 * 4));
	USART2->BRR = SystemCoreClock / 9600;
	USART2->CR1 = (1<<3) | (1<<2) | 1 | 1<<5;
	NVIC->ISER[0] = (1 << USART2_IRQn); // K?ch ho?t ng?t USART2
	
	PWM_Init();
  Encoder_Init();
	volatile uint32_t last = 0;
	volatile uint32_t last_pid_tick = 0; 
	char text[32];
	char t[32];
	
	while (1){
//		if (tuning_done == false) {
//			if (uwTick - last_pid_tick >= 10) {
//				Relay_AutoTune();
//				last_pid_tick = uwTick;
//			}
//		} else {
//			if (is_calculated == false) {
//        Calculate_PID_ZieglerNichols();
//        is_calculated = true;
//			}
//			if (uwTick - last_pid_tick >= 10) {
//				PID_Velocity_Control(); 
//				last_pid_tick = uwTick;
//			}
//		}
		if (new_setpoint_received) {
        float temp_setpoint = atof((const char*)rx_buffer); // Chuy?n chu?i thành float
        
        if (temp_setpoint >= 0 && temp_setpoint <= 1000) { // Gi?i h?n m?t cách an toàn
            Setpoint = temp_setpoint;
        }
        new_setpoint_received = false;
    }
	if (uwTick - last >= 100){
        if (tuning_done == false) {
            sprintf(text, "TUNING... enc=%.1f; pwm=%.1f\r\n", Speed, u);
            for (int i = 0; i < strlen(text); i++) {
                USART2->TDR = text[i];
                while (!(USART2->ISR & (1 << 7)));
            }
        } else {
            sprintf(text, "PID RUN: enc=%.1f; pwm=%.1f\r\n", Speed, u);
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
		float hys = 5.0;     // Hysteresis (Vung trê chông nhiêu: 5 vong/phút)
    volatile float error = Setpoint - Speed;
    if (error > hys) {
        u = d;       
    } else if (error < -hys) {
        u = -d;      
    }
    if (Speed > speed_max) speed_max = Speed;
    if (Speed < speed_min) speed_min = Speed;
    // Do diêm cat & ÐO CHU Ky (Tu)
    if (error_prev > 0 && error <= 0) { 
        crossing_count++;
        if (crossing_count == 1) {//cat lân 1
             t_start = uwTick; //Bat dâu bâm gio
            speed_max = Setpoint; 
            speed_min = Setpoint;
        } else if (crossing_count == 3) {//cat lân 3 //da di duoc 1 vong tr?n tr?n v?n (lên -> xuông -> lên)
            uint32_t t_end = uwTick;
            Tu = (t_end - t_start) / 1000.0; //(ms->s)
            a = (speed_max - speed_min) / 2.0;
						u_prev = u;
						crossing_count = 0;
            tuning_done = true; //Tuning xong!
        }
    }
    error_prev = error;
		e2 = e1;
		e1 = error;
    if (u > 0.0) {
        TIM3->CCR1 = (uint32_t)(u * 10.0);
    } else {
        TIM3->CCR1 = 0;
    }
}

void Calculate_PID_ZieglerNichols() {
		
    volatile float Ku = (4.0 * d) / (3.14159 * a);
    Kp = 0.6 * Ku;
    Ti = 0.5 * Tu;
    Td = 0.125 * Tu;
    Ki = Kp / Ti;
    Kd = Kp * Td;
		
	//test bo Kd
//    Td = 0.0;      
//    Kd = 0.0;
	
		// TÍNH TOÁN H? S? B? L?C KHÂU D (Dùng Backward differences)
    ad = Td / (Td + N_filter * h);
    bd = (Kp * Td * N_filter) / (Td + N_filter * h);

    // K? th?a tr?ng thái cho Bumpless Transfer
    u_prev = u;               
    e1 = Setpoint - Speed;    
    Speed_prev1 = Speed;
    Speed_prev2 = Speed;
    delta_D_prev = 0.0; // Reset s? gia khâu D cu

//    q0 = Kp + Ki * h + (Kd / h);
//    q1 = -(Kp + 2.0 * (Kd / h));
//    q2 = Kd / h;
	
		
}

void PID_Velocity_Control() {
    e0 = Setpoint - Speed;
		// 1. Khâu P (Gi? b = 1): dP = Kp * (e0 - e1)
    float delta_P = Kp * (e0 - e1);

    // 2. Khâu I (Backward): dI = Ki * h * e0
    float delta_I = Ki * h * e0;

    float delta_D = ad * delta_D_prev - bd * (Speed - 2.0 * Speed_prev1 + Speed_prev2);
    delta_D_prev = delta_D; // Luu l?i giá tr? cho chu k? sau
	
    //volatile float delta_u = q0 * e0 + q1 * e1 + q2 * e2;
		volatile float delta_u = delta_P + delta_I + delta_D;
    u = u_prev + delta_u; //Công dôn ra tin hiêu thu tê
		//Anti-Windup
    if (u > 100.0) u = 100.0; // Gi?i h?n max PWM = 100%
    if (u < 0.0)   u = 0.0;   // Gi?i h?n min PWM = 0%
	
    //e2 = e1;
    e1 = e0;
		Speed_prev2 = Speed_prev1; // Ð?y d? li?u lùi v? quá kh?
    Speed_prev1 = Speed;
    u_prev = u;

		TIM3->CCR1 = (uint32_t)(u * 10.0);
}

void USART2_IRQHandler(void)
{
    // Ki?m tra xem ng?t có ph?i do nh?n d? li?u (RXNE) không
    if (USART2->ISR & USART_ISR_RXNE) 
    {
        char c = (char)(USART2->RDR); // Ð?c ký t?
				if (c == 't') {
					tuning_done = false;
            // Ph?i "t?y não" toàn b? bi?n c?a quá trình Tune tru?c
            crossing_count = 0;
            speed_max = 0.0;
            speed_min = 9999.0;
            error_prev = 0.0;
            t_start = 0;
            
            // T?m th?i ng?t d?ng co 1 nh?p d? nó r?t t?c d? xu?ng, 
            // t?o dà cho Ro-le v?t lên b?t dao d?ng chu?n xác
            u = 0; 
            TIM3->CCR1 = 0;
					return;
				}
        
        if (c == '\n' || c == '\r') // Ký t? k?t thúc
        {
            if (rx_index > 0) // Có d? li?u trong buffer
            {
                rx_buffer[rx_index] = '\0'; // K?t thúc chu?i C
                new_setpoint_received = true; 
            }
            rx_index = 0; // Reset index cho l?n nh?n sau
        }
        else if (rx_index < sizeof(rx_buffer) - 1) 
        {
            rx_buffer[rx_index++] = c; // Luu ký t? vào buffer
        }
    }
}