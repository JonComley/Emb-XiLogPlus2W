################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Usb/usb_device.c 

OBJS += \
./Sw/LoggerApp/Microchip/Usb/usb_device.o 

C_DEPS += \
./Sw/LoggerApp/Microchip/Usb/usb_device.d 


# Each subdirectory must supply rules for building sources it contributes
Sw/LoggerApp/Microchip/Usb/usb_device.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Usb/usb_device.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -m32 -std=c99 -D__prog__ -D__PIC24F__ -D_CONFIG3 -D_CONFIG2 -D_CONFIG1 -D__PIC24FJ256GB110__ -D__C30__ -DECLIPSE -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include/MDD File System" -I/opt/microchip/xc16/v1.36/support/PIC24F/h -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include/Usb" -I/opt/microchip/xc16/v1.36/support/PIC24F/gld -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


