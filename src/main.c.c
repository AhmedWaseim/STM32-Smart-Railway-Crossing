/*
 * Reconstructed transcription from the Spring 2025 STM32 railway crossing
 * team project report (Stm.pdf). Not the original project source.
 * NOT COMPILED OR HARDWARE-TESTED. Do not deploy to hardware as-is.
 * Known inconsistencies in report: TIM4_IRQHandler reads TIM2 status;
 * TIM4 is shared by delay, ultrasonic timing and capture; some GPIO
 * comments conflict with register configuration; angle 200 exceeds
 * uint8_t servo's intended 0-180 range. Requires hardware review.
 */

#include <stm32f103xb.h>

#include <math.h>

#include "stm32f1xx_hal.h"

#include "stdio.h"

#define RAD_TO_DEG 57.2957795

float actual_distance = 0;

float desired_distance = 47.0f;

float error, u;

float kp = 200;

static float filtered_distance = 0;

volatile uint8_t emergency_stop = 0;

volatile uint32_t throttle_us = 1500; // From CH3 (PA0)

volatile uint32_t gate_us = 1000;     // From CH5 (PA4)



void debounceDelay() {

    TIM4->PSC = 50 - 1;

    TIM4->ARR = 8000 - 1;

    TIM4->SR = 0;

    TIM4->CR1 = 1;

    while ((TIM4->SR & (1 << 0)) == 0);

    TIM4->CR1 = 0;

}


uint16_t angle_to_pulse(uint8_t angle) {

    return 1000 + ((angle * 1000) / 180);

}



void set_servo_angle(uint8_t angle) {

    TIM2->CCR2 = angle_to_pulse(angle);

}



void delay(int x) {

    TIM4->PSC = x - 1;

    TIM4->ARR = 8000 - 1;

    TIM4->CR1 |= 1;

    while (!(TIM4->SR & 1));

    TIM4->SR &= ~1;

    TIM4->CR1 &= ~1;

    TIM4->ARR = 0xFFFFFFFF;

    TIM4->PSC = 0;

}



void micro_delay(int x) {

    TIM4->PSC = 8 - 1;

    TIM4->ARR = x - 1;

    TIM4->CR1 |= 1;

    while (!(TIM4->SR & 1));


    TIM4->SR &= ~1;

    TIM4->CR1 &= ~1;

    TIM4->ARR = 0xFFFFFFFF;

    TIM4->PSC = 0;

}



float Read_Ultrasonic(void) {

    GPIOA->BSRR = (1 << 9); // Trigger high

    micro_delay(10);

    GPIOA->BRR = (1 << 9); // Trigger low



    TIM4->CNT = 0;

    TIM4->PSC = 8 - 1;

    TIM4->ARR = 60000 - 1;

    TIM4->CR1 |= TIM_CR1_CEN;



    while (!(GPIOA->IDR & (1 << 8)) && !(TIM4->SR & TIM_SR_UIF));

    if (TIM4->SR & TIM_SR_UIF) return -1;



    uint32_t start = TIM4->CNT;

    while ((GPIOA->IDR & (1 << 8)) && !(TIM4->SR & TIM_SR_UIF));

    uint32_t pulse_width = TIM4->CNT - start;

    TIM4->CR1 &= ~TIM_CR1_CEN;



    return (pulse_width * 0.0343f) / 2;


}



void TIM4_IRQHandler(void) {

    static uint32_t ch1_rise = 0, ch3_rise = 0;



    // CH1 - PA0 (Throttle)

    if (TIM2->SR & TIM_SR_CC1IF) {

        if (GPIOA->IDR & (1 << 0)) {

            ch1_rise = TIM2->CCR1; // Rising edge time

            TIM2->CCER |= TIM_CCER_CC1P;// Switch to falling edge

        } else {

            throttle_us = TIM2->CCR1 - ch1_rise;// Pulse width = falling - rising

            TIM2->CCER &= ~TIM_CCER_CC1P; // Reset to rising edge

        }

        TIM2->SR &= ~TIM_SR_CC1IF;

    }



    // CH3 - PA4 (Gate Angle)

    if (TIM4->SR & TIM_SR_CC3IF) {

            if (GPIOB->IDR & (1 << 8)) {

              ch3_rise = TIM4->CCR3;

              TIM4->CCER |= TIM_CCER_CC3P;

            } else {

              gate_us = TIM4->CCR3 - ch3_rise;

              TIM4->CCER &= ~TIM_CCER_CC3P;


         }

         TIM4->SR &= ~TIM_SR_CC3IF;

     }



}



uint16_t map_throttle_to_pwm(uint32_t us) {

    if (us < 1000) us = 1000;

    if (us > 2000) us = 2000;

    return (us - 1000) * 8;

}



void ms_delay(uint32_t ms) {

    for (volatile uint32_t i = 0; i < ms * 8000; i++) __asm("nop");

}



int main(void) {

  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;

 RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_TIM3EN | RCC_APB1ENR_TIM4EN;



    GPIOA->CRL = 0x2A4822A4; // PA0 (input), PA1 (servo), PA4 (input)

    GPIOA->CRH = 0x44442228; // PA7 (buzzer), PA8 (echo), PA9 (trig)

    GPIOB->CRL = 0x84444444;

    GPIOB->CRH = 0x44448884;

  GPIOB->ODR |= (1<<9)|(1 << 10) | (1 << 11) | (1<<7) | (1<<8);



  GPIOA->ODR |= (1 << 2); // motor forward

  GPIOA->ODR &= ~(1 << 3);



  // PWM - DC Motor (PA6 TIM3_CH1)

  TIM3->ARR = 8000;

  TIM3->CCER |= (1 << 0);

  TIM3->CCMR1 = (1 << 3) | (6 << 4);

  TIM3->CR1 = (1 << 0) | (2 << 5) | (1 << 7);

  TIM3->EGR = 1;



  // Servo - PA1 (TIM2_CH2)

  TIM2->PSC = 8 - 1;

  TIM2->ARR = 20000 - 1;

  TIM2->CCR2 = 1500;

  TIM2->CCMR1 |= (6 << 12) | (1 << 11);

  TIM2->CCER |= (1 << 4);

  TIM2->CR1 |= 1;

  TIM2->EGR = 1;



  // --- Input Capture for manual mode ---

  GPIOA->CRL &= ~((0xF << 0) | (0xF << 16));

  GPIOA->CRL |= ((0x4 << 0) | (0x4 << 16)); // PA0, PA4 input floating




     TIM2->CCMR1 |= (1 << 0); // CC1S = 01 (PA0)

     TIM2->CCER |= (1 << 0); // Enable CH1

     TIM2->DIER |= (1 << 1); // CC1 interrupt



     TIM4->CCMR2 &= ~(0xFF);

     TIM4->CCMR2 |= (1 << 0); // CC3S = 01

     TIM4->CCER |= (1 << 8); // CC3E

     TIM4->DIER |= (1 << 3); // CC3 interrupt enable

     TIM4->CR1 |= 1;               // Start TIM4




     NVIC_EnableIRQ(TIM4_IRQn);

 // TIM2->CR1 |= 1; // Start timer




     while (1) {

       if ((GPIOB->IDR & (1 << 10)) == 0) { // AUTOMATIC MODE

          debounceDelay();

          if ((GPIOB->IDR & (1 << 10)) == 0) {

            while (!emergency_stop) {

//                  uint32_t sum = 0;

//                  for (int i = 0; i < 2; i++) {

//                  sum +=Read_Ultrasonic() ;}

//                 actual_distance = sum / 3;

               actual_distance = Read_Ultrasonic();


            filtered_distance = 0.7f * filtered_distance + 0.3f * actual_distance;

            if (filtered_distance < 4) filtered_distance = 4;

            if (filtered_distance > 60) filtered_distance = 60;

          // if (filtered_distance > 39) filtered_distance +=1 ;



            error = desired_distance - filtered_distance;

            u = error * kp;

            if (u > 8000) u = 8000;

            if (u < -8000) u = -8000;



            if (u > 0) {

                GPIOA->ODR |= (1 << 2);

                GPIOA->ODR &= ~(1 << 3);

                TIM3->CCR1 = u;

            } else if (u < 0) {

                GPIOA->ODR &= ~(1 << 2);

                GPIOA->ODR |= (1 << 3);

                TIM3->CCR1 = -u;

            } else {

                TIM3->CCR1 = 0;

                GPIOA->ODR &= ~(1 << 2);

                GPIOA->ODR &= ~(1 << 3);

            }



            if (filtered_distance > 40) {


                  set_servo_angle(0);

                  GPIOA->ODR &= ~(1 << 7);

              } else if (filtered_distance > 17 && filtered_distance <= 40) {

                  set_servo_angle(200);

                  GPIOA->ODR |= (1 << 7);

              } else {

                  set_servo_angle(0);

                  GPIOA->ODR ^= (1 << 7);

                  micro_delay(500);

              }

              // Check emergency stop ES button

                  if ((GPIOB->IDR & (1 << 9)) == 0) {

                         debounceDelay();

                         if ((GPIOB->IDR & (1 << 9)) == 0) {

                                              debounceDelay();

                                          emergency_stop = 1;

                                          GPIOA->ODR|= (1<<10); // red light

                                          GPIOA->ODR &= ~(1 << 11); // turn off green light

                                          // Stop motor and buzzer immediately

                                          TIM3->CCR1 = 0;

                                          GPIOA->ODR &= ~((1 << 2) | (1 << 3)); // Stop motor

                                          GPIOA->ODR &= ~(1 << 7);              //
Buzzer off

                                          break; // Exit mode


                                           }

                   }



                 micro_delay(100);

             }

         }

    }

    // check reset button

    if ((GPIOB->IDR & (1 << 7)) == 0) {

     debounceDelay();

     if ((GPIOB->IDR & (1 << 7)) == 0) {

        emergency_stop = 0;

        GPIOA->ODR|= (1<<11); // green led

        GPIOA->ODR &= ~(1 << 10); // turn off red light

     }

    }



    if ((GPIOB->IDR & (1 << 11)) == 0) { // MANUAL MODE

         debounceDelay();

         if ((GPIOB->IDR & (1 << 11)) == 0) {

                   while (!emergency_stop) {

                       // --- Read throttle & gate pulse widths ---

                       actual_distance = Read_Ultrasonic();

                       if (actual_distance < 2 || actual_distance > 80 || actual_distance < 0) continue;


                 filtered_distance = 0.7f * filtered_distance + 0.3f * actual_distance;



                 // --- Apply throttle from PA0 ---

                 u = map_throttle_to_pwm(throttle_us); // Converts 1000-2000us to 0-8000

                 if (u > 0) {

                     GPIOA->ODR |= (1 << 2);

                     GPIOA->ODR &= ~(1 << 3);

                     TIM3->CCR1 = u;

                 } else {

                     TIM3->CCR1 = 0;

                     GPIOA->ODR &= ~(1 << 2);

                     GPIOA->ODR &= ~(1 << 3);

                 }



                 // --- Gate angle from PB8 (e.g., CH5 VRA knob) ---

                 if (gate_us < 1000) gate_us = 1000;

                 if (gate_us > 2000) gate_us = 2000;

                 uint8_t gate_angle = (gate_us - 1000) * 180 / 1000;

                 set_servo_angle(gate_angle); // Sets TIM2->CCR2



                 // --- Buzzer logic based on gate angle only ---

                 if (gate_angle >= 160) {

                     GPIOA->ODR |= (1 << 7); // Buzzer ON when gate is closed

                 } else if (gate_angle <= 20) {


                       GPIOA->ODR &= ~(1 << 7); // Buzzer OFF when gate is open

                   }

                   // Check emergency stop

                   if ((GPIOB->IDR & (1 << 9)) == 0) {

                           emergency_stop = 1;

                           GPIOA->ODR|= (1<<10); // red light

                            GPIOA->ODR &= ~(1 << 11); // turn off green light

                           // Stop everything

                           TIM3->CCR1 = 0;

                           GPIOA->ODR &= ~((1 << 2) | (1 << 3)); // Motor off

                           GPIOA->ODR &= ~(1 << 7);             // Buzzer off

                           break; // Exit manual mode

                       }



                   ms_delay(50); // Adjust delay as needed

               }




        }

    }

    if ((GPIOB->IDR & (1 << 7)) == 0) {

            debounceDelay();

            if ((GPIOB->IDR & (1 << 7)) == 0) {

            emergency_stop = 0;

            GPIOA->ODR|= (1<<11); // green led


             GPIOA->ODR &= ~(1 << 10); // turn off red light

          }

         }



    }

}
