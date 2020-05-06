/**
 * Esse exemplo demonstra o uso das protothreads.
 * Instala-se 4 tarefas: duas pisca LEDs, uma tarefa
 * que informa que um contador atingiu o valor 1000 e
 * uma tarefa implementando um terminal serial básico.
 */

/* Sempre deve-se incluir o arquivo pt.h ao utilizar protothreads. */
#include "pt.h"
/* Incluir pt-sem.h quando se utiliza semáforos. */
#include "pt-sem.h"

/* Incluir arquivos de header da biblioteca TivaWare para acessar os periféricos do microcontrolador. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_uart.h"
#include "driverlib/rom_map.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/uart.h"

// Constantes requeridas para manipular o temporizador NVIC SysTick
#define NVIC_SYSTICK_CLK        		0x00000004
#define NVIC_SYSTICK_INT        		0x00000002
#define NVIC_SYSTICK_ENABLE     		0x00000001

// Registradores do ARM Cortex-Mx para o NVIC Systick
#define NVIC_SYSTICK_CTRL       		( ( volatile unsigned long *) 0xe000e010)
#define NVIC_SYSTICK_LOAD       		( ( volatile unsigned long *) 0xe000e014)

// Define se as chamadas a funções printf serão permitidas no exemplo
#define USE_PRINTF						1



/* Estruturas de controle das protothreads utilizadas. */
static struct pt pt1, pt2, pt3, pt_serial;
/*---------------------------------------------------------------------------*/
/*
 * A definição a seguir é uma biblioteca simples de temporização
 * utilizada nesse exemplo. A implementação real das funções é
 * encontrada no final desse arquivo.
 */
struct timer { int start, interval; };
static int  timer_expired(struct timer *t);
static void timer_set(struct timer *t, int usecs);
/*---------------------------------------------------------------------------*/
/*
 * Dois temporizadores são utilizados, um para cada tarefa pisca led.
 */
static struct timer timer1, timer2;





/**
 * Primeira tarefa de pisca led.
 * As funções de protothread são executadas pelo laço principal.
 */
static PT_THREAD(protothread1(struct pt *pt)){
  /* Qualquer protothread deve começar com PT_BEGIN(). que recebe
   * o ponteiro de uma estrutura pt. */
  PT_BEGIN(pt);
  /* Como as macros PT_YIELD e PT_YIELD_UNTIL não são utilizadas, evita o warning
   * de compilador devido a variável PT_YIELD_FLAG não estar sendo utilizada.*/
  (void)PT_YIELD_FLAG;

	/* Configura a porta para um LED. */
  	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
	GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_STRENGTH_8MA_SC, GPIO_PIN_TYPE_STD);
	GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_0);

  /* Em laço para sempre */
  while(1) {
	GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, GPIO_PIN_0);
	timer_set(&timer1, 200);
    PT_WAIT_UNTIL(pt, timer_expired(&timer1));

	GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_0, 0);
	timer_set(&timer1, 200);
    PT_WAIT_UNTIL(pt, timer_expired(&timer1));

	#if (USE_PRINTF == 1)
    printf("Protothread 1 está executando.\n\r");
	#endif
  }

  /* Qualquer protothread deve terminar com PT_END(), que recebe
     o ponteiro de uma estrutura pt. */
  PT_END(pt);
}





/**
 * Segunda protothread. Essa função é praticamente a mesma da primeira.
 */
volatile int counter = 0;
static PT_THREAD(protothread2(struct pt *pt)){
	PT_BEGIN(pt);
	(void)PT_YIELD_FLAG;

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);
	GPIOPadConfigSet(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_STRENGTH_8MA_SC, GPIO_PIN_TYPE_STD);
	GPIOPinTypeGPIOOutput(GPIO_PORTN_BASE, GPIO_PIN_1);

  while(1) {
	GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, GPIO_PIN_1);

	timer_set(&timer2, 500);
    PT_WAIT_UNTIL(pt, timer_expired(&timer2));

    GPIOPinWrite(GPIO_PORTN_BASE, GPIO_PIN_1, 0);

    timer_set(&timer2, 500);
    PT_WAIT_UNTIL(pt, timer_expired(&timer2));

	#if (USE_PRINTF == 1)
    printf("Protothread 2 está executando.\n\r");
	#endif

    // Conta a cada execução das duas protothreads pisca LEDs.
    counter++;
  }
  PT_END(pt);
}





/**
 * Terceira protothread. Informa que o pisca LED foi executado 1000 vezes.
 */
static PT_THREAD(protothread3(struct pt *pt)){
  PT_BEGIN(pt);
  (void)PT_YIELD_FLAG;

  while(1) {
    /* Espera até o contador atingir o valor 1000. */
    PT_WAIT_UNTIL(pt, counter == 1000);

    #if (USE_PRINTF == 1)
    printf("O contador atingiu o valor 1000!!!\n\r");
	#endif
    counter = 0;
  }
  PT_END(pt);
}





//*****************************************************************************
// Manipulador da interrupção de UART.
//*****************************************************************************
static struct pt_sem sUART;
volatile char sdata = 0;
void UARTIntHandler(void){
    uint32_t ui32Status;

    // Adquire o estado da interrupção.
    ui32Status = MAP_UARTIntStatus(UART0_BASE, true);

	UARTIntClear(UART0_BASE, ui32Status);

    if ((ui32Status&UART_INT_RX) == UART_INT_RX){
		// Recebe todas os caracteres na FIFO de recepção.
		while(MAP_UARTCharsAvail(UART0_BASE)){
			// Lê o caracter no buffer da UART.
			sdata = (char)MAP_UARTCharGetNonBlocking(UART0_BASE);
		}
    }

    if ((ui32Status&UART_INT_TX) == UART_INT_TX){
    	MAP_UARTIntDisable(UART0_BASE, UART_INT_TX);

    	// Aciona a tarefa do console
    	PT_SEM_SIGNAL(&pt_serial, &sUART);
    }

}


void UARTPutChar(uint32_t ui32Base, char ucData){
	// Envia um caracter.
	HWREG(ui32Base + UART_O_DR) = ucData;

	// Aciona interrupção de transmissão
	MAP_UARTIntEnable(UART0_BASE, UART_INT_TX);
}

#define UARTPutString(ui32Base, string)                                 \
    do{                                                                 \
        while(*string){                                                 \
            UARTPutChar(UART0_BASE, *string++);                         \
            PT_SEM_WAIT(pt, &sUART);                                    \
        }                                                               \
    }while(0)



static PT_THREAD(protothread_serial(struct pt *pt))
{
  PT_BEGIN(pt);
  (void)PT_YIELD_FLAG;

  // Habilita a porta UART 0.
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
  MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

  // Configura os pinos A0 e A1 como pinos de UART.
  MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
  MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
  MAP_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

   // Configura a UART para 115.200, 8 bits, sem paridade e 1 bit de parada..
   MAP_UARTConfigSetExpClk(UART0_BASE,  120000000, 115200,
                           (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                            UART_CONFIG_PAR_NONE));

   MAP_UARTFIFODisable(UART0_BASE);

	// Inicializa o semáforo
	PT_SEM_INIT(&sUART, 0);

   // Habilita a interrupção da UART.
   MAP_IntEnable(INT_UART0);
   MAP_UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);

   static char *string;

   // Limpa o terminal.
   char *str1 = "\033[2J\033[H";
   string = str1;
   while(*string){
	   UARTPutChar(UART0_BASE, *string++);
	   // Espera indefinidamente por uma interrupção de transmissão
	   PT_SEM_WAIT(pt, &sUART);
   }

   // Envie um texto de inicialização do terminal
   char *str2 = "Iniciou!\n\r\n\r";
   string = str2;
   UARTPutString(UART0_BASE, string);


   while(1){
		PT_WAIT_UNTIL(pt, sdata != 0);
		if (sdata != 13){
			/* Se a tecla for diferente de ENTER, devolve o caracter para o terminal. */
			UARTPutChar(UART0_BASE, sdata);
			// Espera indefinidamente por uma interrupção de transmissão
			PT_SEM_WAIT(pt, &sUART);
		}else{
		   /* Quebra de linha. */
		   char *str3 = "\n\r";
		   string = str3;
		   while(*string){
			   UARTPutChar(UART0_BASE, *string++);
			   // Espera indefinidamente por uma interrup��o de transmiss�o
			   PT_SEM_WAIT(pt, &sUART);
		   }
		}
		sdata = 0;
   }

  PT_END(pt);
}


int funcao(void)
{
    static int i = 0;
    for (;i < 10;){
        return i++; /* ponto de saída função */
    }
    i = 0;
    return i++;
}


/**
 * Laço principal. Aqui as protothreads são inicializadas e escalonadas.
 */
int main(void)
{
#if 0
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
    printf("primeira chamada %d\r\n", funcao());
#endif

	/* Esse parâmetro deve ser configurado quando o cristal ultrapassar o
	   valor de 10 MHz. */
	MAP_SysCtlMOSCConfigSet(SYSCTL_MOSC_HIGHFREQ);

	// Configura PLL para clock @ 120 MHz.
    (void)MAP_SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ |
                SYSCTL_OSC_MAIN |
                SYSCTL_USE_PLL |
                SYSCTL_CFG_VCO_480), 120000000);


	// Desabilita o temporizador NVIC systick
	*(NVIC_SYSTICK_CTRL) = 0;
	// Configura o módulo que determina o período do temporizador
    *(NVIC_SYSTICK_LOAD) = 119999;
    // Inicializa o temporizador systick e sua interrupção
    *(NVIC_SYSTICK_CTRL) = NVIC_SYSTICK_CLK | NVIC_SYSTICK_INT | NVIC_SYSTICK_ENABLE;

  /* Inicializa as variáveis de estado das protothreads com PT_INIT(). */
  PT_INIT(&pt1);
  PT_INIT(&pt2);
  PT_INIT(&pt3);
  PT_INIT(&pt_serial);

  /*
   * As protothreads são escalonadas repetidamente chamando-se suas
   * funções e passando o ponteiro da variável de estado com seus
   * argumentos.
   */
  while(1) {
    PT_SCHEDULE(protothread1(&pt1));
    PT_SCHEDULE(protothread2(&pt2));
    PT_SCHEDULE(protothread3(&pt3));
    PT_SCHEDULE(protothread_serial(&pt_serial));
  }
}




/* Funções para implementar o temporizador no sistema. */
int tick_counter = 0;
void TickTimer(void){
	tick_counter++;
}


static int clock_time(void)
{ return (int)tick_counter; }

static int timer_expired(struct timer *t)
{ return (int)(clock_time() - t->start) >= (int)t->interval; }

static void timer_set(struct timer *t, int interval)
{ t->interval = interval; t->start = clock_time(); }

