################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Ana.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cal.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cfs.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cmd.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Com.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cop.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Dig.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Dop.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Frm.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Log.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Mdm.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Msg.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Pdu.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Pwr.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Scf.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Ser.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Slp.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Sns.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Str.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Tim.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Usb.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/alm.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/ftp.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/gps.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/main.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/modbus.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/rtc.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/tsync.c \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/usb_descriptors.c 

OBJS += \
./Firmware/Ana.o \
./Firmware/Cal.o \
./Firmware/Cfs.o \
./Firmware/Cmd.o \
./Firmware/Com.o \
./Firmware/Cop.o \
./Firmware/Dig.o \
./Firmware/Dop.o \
./Firmware/Frm.o \
./Firmware/Log.o \
./Firmware/Mdm.o \
./Firmware/Msg.o \
./Firmware/Pdu.o \
./Firmware/Pwr.o \
./Firmware/Scf.o \
./Firmware/Ser.o \
./Firmware/Slp.o \
./Firmware/Sns.o \
./Firmware/Str.o \
./Firmware/Tim.o \
./Firmware/Usb.o \
./Firmware/alm.o \
./Firmware/ftp.o \
./Firmware/gps.o \
./Firmware/main.o \
./Firmware/modbus.o \
./Firmware/rtc.o \
./Firmware/tsync.o \
./Firmware/usb_descriptors.o 

C_DEPS += \
./Firmware/Ana.d \
./Firmware/Cal.d \
./Firmware/Cfs.d \
./Firmware/Cmd.d \
./Firmware/Com.d \
./Firmware/Cop.d \
./Firmware/Dig.d \
./Firmware/Dop.d \
./Firmware/Frm.d \
./Firmware/Log.d \
./Firmware/Mdm.d \
./Firmware/Msg.d \
./Firmware/Pdu.d \
./Firmware/Pwr.d \
./Firmware/Scf.d \
./Firmware/Ser.d \
./Firmware/Slp.d \
./Firmware/Sns.d \
./Firmware/Str.d \
./Firmware/Tim.d \
./Firmware/Usb.d \
./Firmware/alm.d \
./Firmware/ftp.d \
./Firmware/gps.d \
./Firmware/main.d \
./Firmware/modbus.d \
./Firmware/rtc.d \
./Firmware/tsync.d \
./Firmware/usb_descriptors.d 


# Each subdirectory must supply rules for building sources it contributes
Firmware/Ana.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Ana.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Cal.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cal.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Cfs.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cfs.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Cmd.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cmd.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Com.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Com.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Cop.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Cop.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Dig.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Dig.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Dop.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Dop.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Frm.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Frm.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Log.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Log.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Mdm.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Mdm.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Msg.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Msg.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Pdu.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Pdu.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Pwr.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Pwr.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Scf.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Scf.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Ser.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Ser.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Slp.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Slp.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Sns.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Sns.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Str.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Str.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Tim.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Tim.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/Usb.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/Usb.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/alm.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/alm.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/ftp.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/ftp.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/gps.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/gps.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/main.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/main.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/modbus.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/modbus.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/rtc.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/rtc.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/tsync.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/tsync.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Firmware/usb_descriptors.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware/usb_descriptors.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D__PIC24F__ -I/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


