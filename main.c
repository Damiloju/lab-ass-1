/**
 * @brief Example usage of GPIO peripheral. Three LEDs are toggled using
 * GPIO functionality. A hardware-to-software interrupt is set up and
 * triggered by a button switch.
 *
 * The tsb0 board has three LEDs (red, green, blue) connected to ports
 * PB11, PB12, PA5 respectively. The button switch is connected to port
 * PF4. LED and button locations (pin and port numbers) can be found from
 * the tsb0 board wiring schematics.
 *
 * EFR32 Application Note on GPIO
 * https://www.silabs.com/documents/public/application-notes/an0012-efm32-gpio.pdf
 *
 * EFR32MG12 Wireless Gecko Reference Manual (GPIO p1105)
 * https://www.silabs.com/documents/public/reference-manuals/efr32xg12-rm.pdf
 *
 * GPIO API documentation
 * https://docs.silabs.com/mcu/latest/efr32mg12/group-GPIO
 *
 * ARM RTOS API
 * https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS.html
 *
 * Copyright Thinnect Inc. 2019
 * Copyright ProLab TTÜ 2022
 * @license MIT
 * @author Johannes Ehala
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "retargetserial.h"
#include "cmsis_os2.h"
#include "platform.h"

#include "SignatureArea.h"
#include "DeviceSignature.h"

#include "loggers_ext.h"
#include "logger_fwrite.h"

#include "em_cmu.h"
#include "em_gpio.h"

#include "loglevels.h"
#define __MODUUL__ "main"
#define __LOG_LEVEL__ (LOG_LEVEL_main & BASE_LOG_LEVEL)
#include "log.h"

// Include the information header binary
#include "incbin.h"
INCBIN(Header, "header.bin");

#define ESWGPIO_EXTI_INDEX 4         // External interrupt number 4.
#define ESWGPIO_EXTI_IF 0x00000010UL // Interrupt flag for external interrupt

// declare setup functions
void set_up_pins();
void set_up_tasks();

// declare buzzer functions
void buzzer_loop();
void buzzer_loop_two();

// declare button function
void button_loop();

// declare initGPIOButton funtion
void initGPIOButton();
void buttonIntEnable();

// initialize var to hold button task id
osThreadId_t button_task_id;

// initialize var to hold buzzer task id (which will be used later to suspend the buzzer task)
osThreadId_t buzzer_task_id;
osThreadId_t buzzer_task_two_id;

// declare flag to resume thread
static const uint32_t buttonExtIntThreadFlag = 0x00000001;

// Declaration of enum of boolean values
typedef enum
{
    F,
    T
} boolean;

// declare variable to keep buzzer task state
boolean buzzer_task_started = F;

// Heartbeat thread, initialize GPIO and print heartbeat messages.
void hp_loop()
{
#define ESWGPIO_HB_DELAY 10 // Heartbeat message delay, seconds

    // TODO Initialize GPIO.
    CMU_ClockEnable(cmuClock_GPIO, true);

    // Set up pins to be used for interrupt and buzzer
    set_up_pins();

    // set up threads/tasks
    set_up_tasks();

    // Initialize GPIO interrupt for button
    initGPIOButton();

    // Enable button interrupt
    buttonIntEnable();

    for (;;)
    {
        osDelay(ESWGPIO_HB_DELAY * osKernelGetTickFreq());
        info1("Heartbeat");
    }
}

void set_up_pins()
{
    // Set up buzzer pins
    GPIO_PinModeSet(gpioPortA, 0, gpioModePushPull, 0);
    // Set up the pins for the Buttons
    GPIO_PinModeSet(gpioPortF, 4, gpioModeInputPullFilter, 1);
}

void set_up_tasks()
{
    // create a thread/task for buzzer
    const osThreadAttr_t BUZZER_thread_attr = {.name = "BUZZER_thread_attr"};
    buzzer_task_id = osThreadNew(buzzer_loop, NULL, &BUZZER_thread_attr);

    // create a thread/task for buzzer tone two
    const osThreadAttr_t BUZZER_thread_two_attr = {.name = "BUZZER_thread_two_attr"};
    buzzer_task_two_id = osThreadNew(buzzer_loop_two, NULL, &BUZZER_thread_two_attr);

    // Create a thread/task.
    const osThreadAttr_t button_thread_attr = {.name = "button"};
    button_task_id = osThreadNew(button_loop, NULL, &button_thread_attr);
}

// buzzer task.
void buzzer_loop()
{
    for (;;)
    {
        // wait for 70 os ticks
        osDelay(70);

        // toggle buzzer pin
        GPIO_PinOutToggle(gpioPortA, 0);

        // set start to true
        buzzer_task_started = T;

        // log out for debugging
        info1("Buzzer tone played");
    }
}

// buzzer task tone two.
void buzzer_loop_two()
{
    for (;;)
    {
        // wait for 40 os ticks
        osDelay(40);

        // toggle buzzer pin
        GPIO_PinOutToggle(gpioPortA, 0);

        // log out for debugging
        info1("Buzzer tone two played");
    }
}

// button interrupt task
void button_loop(void *args)
{
    for (;;)
    {
        osThreadFlagsClear(buttonExtIntThreadFlag);
        osThreadFlagsWait(buttonExtIntThreadFlag, osFlagsWaitAny, osWaitForever);

        // do smt
        info1("Button Interrupt toggled");

        // suspend and resume buzzer tasks based on the previous state of buzzer_task_started
        if (buzzer_task_started)
        {
            // suspend buzzer tasks if they are running/allowed to run
            osThreadSuspend(buzzer_task_id);
            osThreadSuspend(buzzer_task_two_id);
            buzzer_task_started = F;
            info1("Buzzer tasks suspended");
        }
        else
        {
            // resume buzzer tasks if they are suspended
            osThreadResume(buzzer_task_id);
            osThreadResume(buzzer_task_two_id);
            buzzer_task_started = T;
            info1("Buzzer tasks resumed");
        }
    }
}

int logger_fwrite_boot(const char *ptr, int len)
{
    fwrite(ptr, len, 1, stdout);
    fflush(stdout);
    return len;
}

int main()
{
    PLATFORM_Init();

    // Configure log message output
    RETARGET_SerialInit();
    log_init(BASE_LOG_LEVEL, &logger_fwrite_boot, NULL);

    info1("ESW-GPIO " VERSION_STR " (%d.%d.%d)", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    // Initialize OS kernel.
    osKernelInitialize();

    // Create a thread.
    const osThreadAttr_t hp_thread_attr = {.name = "hp"};
    osThreadNew(hp_loop, NULL, &hp_thread_attr);

    if (osKernelReady == osKernelGetState())
    {
        // Switch to a thread-safe logger
        logger_fwrite_init();
        log_init(BASE_LOG_LEVEL, &logger_fwrite, NULL);

        // Start the kernel
        osKernelStart();
    }
    else
    {
        err1("!osKernelReady");
    }

    for (;;)
        ;
}

// Button interrupt thread.
void initGPIOButton()
{
    GPIO_IntDisable(ESWGPIO_EXTI_IF); // Disable before config to avoid unwanted interrupt trigerring

    GPIO_ExtIntConfig(gpioPortF, 4, ESWGPIO_EXTI_INDEX, false, true, false); //  port , pin, EXTI number, rising edge, falling edge enabled

    GPIO_InputSenseSet(GPIO_INSENSE_INT, GPIO_INSENSE_INT);
}

void buttonIntEnable()
{
    GPIO_IntClear(ESWGPIO_EXTI_IF);

    NVIC_EnableIRQ(GPIO_EVEN_IRQn);
    NVIC_SetPriority(GPIO_EVEN_IRQn, 3);

    GPIO_IntEnable(ESWGPIO_EXTI_IF);
}

void GPIO_EVEN_IRQHandler(void)
{
    // Get all pending and enabled interrupts.
    uint32_t pending = GPIO_IntGetEnabled();

    // Check if button interrupt is enabled
    if (pending & ESWGPIO_EXTI_IF)
    {
        // clear interrupt flag.
        GPIO_IntClear(ESWGPIO_EXTI_IF);

        // Trigger button thread to resume.
        osThreadFlagsSet(button_task_id, buttonExtIntThreadFlag);
    }
    else
        ; // This was not a button interrupt.
}
