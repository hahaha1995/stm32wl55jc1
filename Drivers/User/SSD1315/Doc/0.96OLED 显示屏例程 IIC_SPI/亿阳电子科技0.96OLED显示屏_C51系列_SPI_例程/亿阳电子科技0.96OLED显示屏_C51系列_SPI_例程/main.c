//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//深圳市亿阳电子科技有限公司
//              说明: 
//              GND  电源地
//              VCC  接5V或3.3v电源
//              D0   P1^0（SCL）
//              D1   P1^1（SDA）
//              RES  接P12
//              DC   接P13
//              CS   接P14               
//All rights reserved
//******************************************************************************/
#include "REG51.h"
#include "oled.h"
#include "bmp.h"

 int main(void)
 {	u8 t;
	OLED_Init();		      	  //初始化OLED  
	while(1) 
	{		
		OLED_Clear();
		OLED_ShowCHinese(0+9,0,0);//亿阳电子科技
		OLED_ShowCHinese(18+9,0,1);
		OLED_ShowCHinese(36+9,0,2);
		OLED_ShowCHinese(54+9,0,3);
		OLED_ShowCHinese(72+9,0,4);
		OLED_ShowCHinese(90+9,0,5);

		OLED_ShowString(0,2," 1.3'OLED TEST");
	 	OLED_ShowString(20,4,"2018/08/24");  
		OLED_ShowString(33,6,"CODE:");  
		t++;
		if(t>'~')t=' ';
		OLED_ShowNum(73,6,t,3,16);   //显示ASCII字符的码值 	
			
		delay_ms(50);
		OLED_Clear();
		delay_ms(50);
		OLED_DrawBMP(0,0,128,8,BMP1);//图片显示(图片显示慎用，生成的字表较大，会占用较多空间，FLASH空间8K以下慎用)
		delay_ms(50);
		OLED_Clear();
		OLED_DrawBMP(0,0,128,8,BMP2);
		delay_ms(50);
	}	  	
}

