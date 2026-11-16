     1	#include <nds.h>
     2	
     3	
     4	int main() {
     5	
     6		float rotateX = 0.0;
     7		float rotateY = 0.0;
     8	
     9		//set mode 0, enable BG0 and set it to 3D
    10		videoSetMode(MODE_0_3D);
    11	
    12		// initialize gl
    13		glInit();
    14	
    15		// enable antialiasing
    16		glEnable(GL_ANTIALIAS);
    17	
    18		// setup the rear plane
    19		glClearColor(0,0,0,31); // BG must be opaque for AA to work
    20		glClearPolyID(63); // BG must have a unique polygon ID for AA to work
    21		glClearDepth(0x7FFF);
    22	
    23		//this should work the same as the normal gl call
    24		glViewport(0,0,255,191);
    25	
    26		//any floating point gl call is being converted to fixed prior to being implemented
    27		glMatrixMode(GL_PROJECTION);
    28		glLoadIdentity();
    29		gluPerspective(70, 256.0 / 192.0, 0.1, 40);
    30	
    31		gluLookAt(	0.0, 0.0, 1.0,		//camera possition
    32					0.0, 0.0, 0.0,		//look at
    33					0.0, 1.0, 0.0);		//up
    34	
    35		while(pmMainLoop()) {
    36			glPushMatrix();
    37	
    38			//move it away from the camera
    39			glTranslatef32(0, 0, floattof32(-1));
    40	
    41			glRotateX(rotateX);
    42			glRotateY(rotateY);
    43	
    44	
    45			glMatrixMode(GL_MODELVIEW);
    46	
    47	
    48	
    49			//not a real gl function and will likely change
    50			glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
    51	
    52			scanKeys();
    53	
    54			u16 keys = keysHeld();
    55	
    56			if((keys & KEY_UP)) rotateX += 3;
    57			if((keys & KEY_DOWN)) rotateX -= 3;
    58			if((keys & KEY_LEFT)) rotateY += 3;
    59			if((keys & KEY_RIGHT)) rotateY -= 3;
    60	
    61	
    62			//draw the obj
    63			glBegin(GL_TRIANGLE);
    64	
    65				glColor3b(255,0,0);
    66				glVertex3v16(inttov16(-1),inttov16(-1),0);
    67	
    68				glColor3b(0,255,0);
    69				glVertex3v16(inttov16(1), inttov16(-1), 0);
    70	
    71				glColor3b(0,0,255);
    72				glVertex3v16(inttov16(0), inttov16(1), 0);
    73	
    74			glEnd();
    75	
    76			glPopMatrix(1);
    77	
    78			glFlush(0);
    79	
    80			swiWaitForVBlank();
    81	
    82			if(keys & KEY_START) break;
    83		}
    84	
    85		return 0;
    86	}
