#include "app.h"
#include "main.h"          // pin labels: LED_ON_Pin, LED_ON_GPIO_Port
#include "usart.h"         // huart3
#include "taskScheduler.h"
#include "GUartStream.h"

static commandProcessor cp;
static taskScheduler    sched(&cp);
static GUartStream      console(&huart3);   // <-- verify signature, see note

static void heartbeat(void)
{
    HAL_GPIO_TogglePin(LED_ON_GPIO_Port, LED_ON_Pin);
}

void appSetup(void)
{
    cp.registerStream(&console);
    cp.registerCommands(sched.schedulerCommands());
    sched.addTask("HEARTBEAT", heartbeat, 500);
}

void appLoop(void)
{
    cp.processStreams();
    cp.processCommands();
    sched.run();
}