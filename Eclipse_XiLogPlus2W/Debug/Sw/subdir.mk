################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/Eclipse_XiLogPlus2W.c 

OBJS += \
./Sw/Eclipse_XiLogPlus2W.o 

C_DEPS += \
./Sw/Eclipse_XiLogPlus2W.d 


# Each subdirectory must supply rules for building sources it contributes
Sw/Eclipse_XiLogPlus2W.o: /home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/Eclipse_XiLogPlus2W.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -m32 -D__prog__ -D__PIC24F__ -D_CONFIG3 -D_CONFIG2 -D_CONFIG1 -D__PIC24FJ256GB110__ -D__C30__ -DECLIPSE -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Firmware" -I/usr/include/x86_64-linux-gnu -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include/MDD File System" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Sw/LoggerApp/Microchip/Include/Usb" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Eclipse_XiLogPlus2W/include" -I"/home/temp/eclipse-workspace/Emb-XiLogPlus2W/Eclipse_XiLogPlus2W/support" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


