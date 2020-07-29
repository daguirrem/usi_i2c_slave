/*
 * File:   i2c.c
 * Autor:  David A. Aguirre Morales - david.aguirre1598@outlook.com
 *
 * Fecha de creación:   23 de junio de 2020, 08:33 PM
 * Última modificación: 25 de julio de 2020
 *			implementación de funciones para leer distintos tipos de datos
 *			en los registros.
 *			Actualización a completo
 *
 * Descripción :
 *  Libreria para la implementación del periferico USI en modo I²C.
 *  Declaración de funciones e interrupciones.
 *
 * Estado:
 *  Funcionalidad aprobada.
 *
 * Futuras actualizaciones:
 *  Ninguna
 */

/* REFERENCIAS:
 *  Understanding I²C, Texas Instruments (https://www.ti.com/lit/an/slva704/slva704.pdf).
 *  ATtiny45 DATASHEET, Atmel (https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-2586-AVR-8-bit-Microcontroller-ATtiny25-ATtiny45-ATtiny85_Datasheet.pdf)
 */

#include <stdint.h>
#include "usi_i2c_slave.h"
#include <avr/interrupt.h>

static uint8_t status = 0;	/*Status (Estado Actual)		*/
static uint8_t rdir   = 0;	/*Register direction (Dirección actual)	*/
static uint8_t ack    = 0;	/*ACK (Indicador de modo ACK)		*/

/*Interrupciones*/

/*Interrupción por detección de START*/
ISR(USI_START_vect){
    /*Mantener SCL*/
    I2CD |= ( 1<<SCLP );
    /*¿Repeated START?*/
    if (status == 2) {
	/*Si, Vuelva al inicio para que lea de nuevo la dirección*/
        status = 0;
    }
    else {
	/*No, prepare la interrupción por desborde*/
        USICR |= (1<<USIOIE);
        USIDR = 0;
    }
    /*Reinicio de todas la banderas y del contador*/
    USISR =  ~( (1<<USICNT3)|(1<<USICNT2)|(1<<USICNT1)|(1<<USICNT0) );
    /*Liberar SCL*/
    I2CD &= ~(( 1<<SCLP ));
}

/*Interrupción por desborde de contador*/
ISR(USI_OVF_vect){

    /*¿Modo ACK? (¿Estoy en el bit correspondiente al ACK?)*/
    if(ack){

	/*¿NACK o ACK? (por parte del maestro)*/
	if(status == 5){
	    if ( bit_is_clear(I2CPN,SDAP)) {
		/*En caso de ACK, prepare el siguiente envío del registro*/
		status = 4;
		I2CD |=  ( 1<<SDAP );
		rdir++;
		loop_until_bit_is_clear(I2CPN,SCLP);
	    }
	    else {
		/*En caso de NACK, termine la trasmisión*/
		loop_until_bit_is_clear(I2CPN,SCLP);
		status = 0;
		rdir = 0;
		I2CP &= ~(( 1<<SDAP ));
		I2CD &= ~(( 1<<SDAP ));
		USICR &= ~(1<<USIOIE);
	    }
	}
	/*Modo recepción de datos (POST ACK)*/
	if(status == 3) {
	    /*Mantener SCL en bajo*/
            I2CD |=  ( 1<<SCLP );
	    /*¿Stop?*/
	    if(bit_is_set(USISR,USIPF)){
		/*Si, Detenga la trasmisión*/
		rdir = 0;
		status = 0;
		USICR &= ~(1<<USIOIE);
	    }
	    else {
		/*No, Prepare el siguiente registro*/
		status=2;
		rdir++;
	    }
	    /*Liberar SDA, para el resto de modos*/
            I2CD &= ~(( 1<<SDAP ));
        }
	/*Modo envío de datos (PRE)*/
	else if ( status == 4) {
	     /*Mantener SCL en bajo*/
	    I2CD |=  ( 1<<SCLP );
	    /*Cargue el registro de salido con los datos*/
            USIDR = i2c_slave.registers[rdir];
	    /*SDA como salida, para envío*/
            I2CP |=  ( 1<<SDAP );
        }
        else {
	    /*Mantener SCL en bajo*/
	    I2CD |=  ( 1<<SCLP );
	    /*Liberar SDA, para el resto de modos*/
            I2CD &= ~(( 1<<SDAP ));
        }

	/*Alterne el modo ACK*/
	ack = 0;
        /*Reinicio de todas la banderas y del contador*/
	USISR =  ~( ( 1<<USICNT3 )|( 1<<USICNT2 )|( 1<<USICNT1 )|( 1<<USICNT0 ) );
    }
    else {
	/*Mantener SCL en bajo*/
	I2CD |=  ( 1<<SCLP );
	/*Reiniciar únicamente el contador*/
	USISR = ~USISR & ~( ( 1<<USICNT3 )|( 1<<USICNT2 )|( 1<<USICNT1 )|( 1<<USICNT0 ) );

	/*Lectura de direccion (Esclavo) y modo (Escribir o Leer)*/
	if(status == 0){
            uint8_t wrrd = USIDR&0x1;	/*Escribir o leer*/
            uint8_t dire = USIDR>>1;	/*Dirección leída del maestro*/

	    /*¿El maestro envió mi dirección?*/
	    if(dire == i2c_slave.direction) {
		/*Compruebe si el maestro quiere escribir o leer*/
		if(wrrd == 1) {
		    /*Si quiere leer, active el modo envío de datos*/
		    status = 4;
		}
		else {
		    /*Si no, lea el registro objetivo*/
		    status++;
		}
		/*Prepare el modo ACK*/
		I2CD |=  ( 1<<SDAP );
		ack = 1;
	    }
        }
	/*Lectura de dirección de registro objetivo*/
        else if(status == 1) {
            /*Guarde la dirección del registro objetivo*/
	    rdir = USIDR;
	    /*Prepare el modo ACK*/
            I2CD |=  ( 1<<SDAP );
            ack = 1;
	    /*Prepare modo recepción de datos (PRE ACK)*/
            status++;
        }
	/*Modo recepción de datos (PRE ACK)*/
        else if (status == 2) {
            /*Guarde los datos enviados por el maestro en la dirección dada*/
	    i2c_slave.registers[rdir] = USIDR;
	    /*Prepare el modo ACK*/
            I2CD |= ( 1<<SDAP );
	    ack = 1;
            /*Prepare modo recepción de datos (POST ACK)*/
	    status++;
        }
	/*Modo de envió de datos (PRE ACK)*/
        else if(status == 4) {
            /*Prepare la interrupción al siguiente flanco de subida en SCL*/
	    /*(Flanco correspondiente al ACK)*/
	    USISR |= ( 1<<USICNT0 );	    /*14+1 = 15*/
	    /*Modo ACK*/
	    I2CD  &= ~( 1<<SDAP);
	    ack = 1;
	    /*Prepare lectura de ACK o NACK*/
	    status++;
        }

	/*Si el modo ACK fue configurado inicialice el contador en 14*/
	/*para provocar una interrupción en el siguiente clock en SCL*/
        if(ack) {
            USISR |= ( 1<<USICNT3 )|( 1<<USICNT2 )|( 1<<USICNT1 );
        }
	/*Limpieza buffer entrada*/
        USIDR    = 0;
	/*Limpieza banderas de interrupción*/
        USISR	|= ( 1<<USIOIF )|( 1<<USISIF ) ;
    }
    /*Liberar SCL*/
    I2CD &= ~( 1<<SCLP );
}

void usi_i2c_slave(uint8_t dir){
    I2CP &= ~(( 1<<SDAP ) | ( 1<<SCLP ));   /*Configuración pines SDA y SCL*/
    I2CD &= ~(( 1<<SDAP ) | ( 1<<SCLP ));

    USICR =  ( 1<<USISIE )|                 /*Interrupción START*/
             ( 1<<USIWM1 )|                 /*Modo I²C*/
             ( 1<<USICS1 );                 /*con fuente de reloj externo*/

    USISR =  ( 1<<USISIF )|                 /*Limpieza de banderas*/
             ( 1<<USIOIF )|
             ( 1<<USIPF  )|
             ( 1<<USIDC  );

    i2c_slave.direction = dir;              /*Asignación de dirección*/
    sei();                                  /*Interrupciones globales*/
}

void usi_i2c_save_registers_u8(uint8_t data, uint8_t dir) {
    uint8_t * registers;
    registers = (uint8_t*) (i2c_slave.registers + dir);
    *registers = data;
}
void usi_i2c_save_registers_u16(uint16_t data, uint8_t dir) {
    uint16_t * registers;
    registers = (uint16_t*) (i2c_slave.registers + dir);
    //Intercambiar posición de bytes
    *registers = ((data&0xFF)<<8)|((data&0xFF00)>>8);
}
void usi_i2c_save_registers_u32(uint32_t data, uint8_t dir) {
    uint32_t * registers;
    registers = (uint32_t*) (i2c_slave.registers + dir);
    //Intercambiar posición de bytes
    *registers = ((data&0x000000FF)<<24)|((data&0xFF000000)>>24)|
		 ((data&0x0000FF00)<< 8)|((data&0x00FF0000)>> 8);
}

void usi_i2c_save_registers_s8(int8_t data, uint8_t dir) {
    int8_t * registers;
    registers = (int8_t*) (i2c_slave.registers + dir);
    *registers = data;
}
void usi_i2c_save_registers_s16(int16_t data, uint8_t dir) {
    int16_t * registers;
    registers = (int16_t*) (i2c_slave.registers + dir);
    //Intercambiar posición de bytes
    *registers = ((data&0xFF)<<8)|((data&0xFF00)>>8);
}
void usi_i2c_save_registers_s32(int32_t data, uint8_t dir) {
    int32_t * registers;
    registers = (int32_t*) (i2c_slave.registers + dir);
    //Intercambiar posición de bytes
    *registers = ((data&0x000000FF)<<24)|((data&0xFF000000)>>24)|
		 ((data&0x0000FF00)<< 8)|((data&0x00FF0000)>> 8);
}

uint8_t usi_i2c_read_registers_u8 (uint8_t dir) {
    uint8_t * registers;
    registers = (uint8_t*) (i2c_slave.registers + dir);
    return *registers ;
}
uint16_t usi_i2c_read_registers_u16(uint8_t dir) {
    uint16_t * registers;
    registers = (uint16_t*) (i2c_slave.registers + dir);
    return *registers;
}
uint32_t usi_i2c_read_registers_u32(uint8_t dir) {
    uint32_t * registers;
    registers = (uint32_t*) (i2c_slave.registers + dir);
    return *registers;
}
int8_t usi_i2c_read_registers_s8 (uint8_t dir) {
    int8_t * registers;
    registers = (int8_t*) (i2c_slave.registers + dir);
    return *registers;
}
int16_t usi_i2c_read_registers_s16(uint8_t dir) {
    int16_t * registers;
    registers = (int16_t*) (i2c_slave.registers + dir);
    return *registers;
}
int32_t usi_i2c_read_registers_s32(uint8_t dir) {
    int32_t * registers;
    registers = (int32_t*) (i2c_slave.registers + dir);
    return *registers;
}
