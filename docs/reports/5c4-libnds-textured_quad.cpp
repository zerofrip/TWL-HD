     1	#include <nds.h>
     2	#include <stdlib.h>
     3	
     4	//texture_bin.h is created automagicaly from the texture.bin placed in arm9/resources
     5	//texture.bin is a raw 128x128 16 bit image.  I will release a tool for texture conversion
     6	//later
     7	#include "texture_bin.h"
     8	
     9	int main() {
    10	
    11		int textureID;
    12	
    13		float rotateX = 0.0;
    14		float rotateY = 0.0;
    15	
    16		//set mode 0, enable BG0 and set it to 3D
    17		videoSetMode(MODE_0_3D);
    18	
    19		// initialize gl
    20		glInit();
    21	
    22		//enable textures
    23		glEnable(GL_TEXTURE_2D);
    24	
    25		// enable antialiasing
    26		glEnable(GL_ANTIALIAS);
    27	
    28		// setup the rear plane
    29		glClearColor(0,0,0,31); // BG must be opaque for AA to work
    30		glClearPolyID(63); // BG must have a unique polygon ID for AA to work
    31		glClearDepth(0x7FFF);
    32	
    33		//this should work the same as the normal gl call
    34		glViewport(0,0,255,191);
    35	
    36		vramSetBankA(VRAM_A_TEXTURE);
    37	
    38		glGenTextures(1, &textureID);
    39		glBindTexture(0, textureID);
    40		glTexImage2D(0, 0, GL_RGB, TEXTURE_SIZE_128 , TEXTURE_SIZE_128, 0, TEXGEN_TEXCOORD, (u8*)texture_bin);
    41	
    42	
    43		//any floating point gl call is being converted to fixed prior to being implemented
    44		glMatrixMode(GL_PROJECTION);
    45		glLoadIdentity();
    46		gluPerspective(70, 256.0 / 192.0, 0.1, 40);
    47	
    48		gluLookAt(	0.0, 0.0, 1.0,		//camera possition
    49					0.0, 0.0, 0.0,		//look at
    50					0.0, 1.0, 0.0);		//up
    51	
    52		while(pmMainLoop()) {
    53			glMatrixMode(GL_MODELVIEW);
    54			glPushMatrix();
    55	
    56			//move it away from the camera
    57			glTranslatef32(0, 0, floattof32(-1));
    58	
    59			glRotateX(rotateX);
    60			glRotateY(rotateY);
    61	
    62	
    63	
    64			glMaterialf(GL_AMBIENT, RGB15(16,16,16));
    65			glMaterialf(GL_DIFFUSE, RGB15(16,16,16));
    66			glMaterialf(GL_SPECULAR, BIT(15) | RGB15(8,8,8));
    67			glMaterialf(GL_EMISSION, RGB15(16,16,16));
    68	
    69			//ds uses a table for shinyness..this generates a half-ass one
    70			glMaterialShinyness();
    71	
    72			//not a real gl function and will likely change
    73			glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK);
    74	
    75			scanKeys();
    76	
    77			u16 keys = keysHeld();
    78	
    79			if((keys & KEY_UP)) rotateX += 3;
    80			if((keys & KEY_DOWN)) rotateX -= 3;
    81			if((keys & KEY_LEFT)) rotateY += 3;
    82			if((keys & KEY_RIGHT)) rotateY -= 3;
    83	
    84			glBindTexture(0, textureID);
    85	
    86			//draw the obj
    87			glBegin(GL_QUAD);
    88				glNormal(NORMAL_PACK(0,inttov10(-1),0));
    89	
    90				GFX_TEX_COORD = (TEXTURE_PACK(0, inttot16(128)));
    91				glVertex3v16(floattov16(-0.5),	floattov16(-0.5), 0 );
    92	
    93				GFX_TEX_COORD = (TEXTURE_PACK(inttot16(128),inttot16(128)));
    94				glVertex3v16(floattov16(0.5),	floattov16(-0.5), 0 );
    95	
    96				GFX_TEX_COORD = (TEXTURE_PACK(inttot16(128), 0));
    97				glVertex3v16(floattov16(0.5),	floattov16(0.5), 0 );
    98	
    99				GFX_TEX_COORD = (TEXTURE_PACK(0,0));
   100				glVertex3v16(floattov16(-0.5),	floattov16(0.5), 0 );
   101	
   102			glEnd();
   103	
   104			glPopMatrix(1);
   105	
   106			glFlush(0);
   107	
   108			swiWaitForVBlank();
   109	
   110			if(keys & KEY_START) break;
   111		}
   112	
   113		return 0;
   114	}//end main
