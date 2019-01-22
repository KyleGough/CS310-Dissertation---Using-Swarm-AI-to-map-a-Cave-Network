#include <GL/glut.h>
#include <iostream>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string>
#include <cstring>
#include <cstdint>

#include "SimplexNoise.h" //Perlin Noise.
#include "Draw.h" //Draw functions.
using namespace std;

/*@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@*/

//Light Struct.
struct Light {
	size_t name;
	float ambient[4];
	float diffuse[4];
	float specular[4];
	float position[4];
};

//Material Struct.
struct Material {
	float ambient[4];
	float diffuse[4];
	float specular[4];
	float shininess;
};

//Global Light Source.
const Light globalLight = {
	GL_LIGHT0,
	{0.2f, 0.2f, 0.2f, 1.0f},
	{2.0f, 2.0f, 2.0f, 1.0f},
	{0.5f, 0.5f, 0.5f, 1.0f},
	{120.0f, 90.0f, 50.0f, 1.0f}
};

//Global Material.
const Material globalMaterial = {
	{0.02f, 0.02f, 0.02f, 1.0f},
  {0.01f, 0.01f, 0.01f, 1.0f},
	{0.4f, 0.4f, 0.4f, 1.0f},
	0.078125f

	/*{0.15f, 0.15f, 0.15f, 1.0f},
  {0.4f, 0.4f, 0.4f, 1.0f},
	{0.7746f, 0.7746f, 0.7746f, 1.0f},
	76.8f*/
};

//Sets the properties of a given material.
void setMaterial(const Material& material) {
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, material.ambient);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material.diffuse);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, material.specular);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, material.shininess);
}

//Sets the properties of a given light.
void setLight(const Light& light) {
	glLightfv(light.name, GL_AMBIENT, light.ambient);
	glLightfv(light.name, GL_DIFFUSE, light.diffuse);
	glLightfv(light.name, GL_SPECULAR, light.specular);
	glLightfv(light.name, GL_POSITION, light.position);
	//glLightf(light.name, GL_SPOT_CUTOFF, 60.0f);
}

/*@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@*/

//Cave Properties.
const int caveWidth = 250; //Number of cells making the width of the cave.
const int caveHeight = 180; //Number of cells making the height of the cave.
const int border = 3; //Padding of the cave border on the x-axis.

//Generation Parameters.
int fillPercentage = 45; //Percentage of the randomised environment that will be filled.
const int birthThreshold = 4;
const int deathThreshold = 4;
const int deathChance = 75;
const int birthChance = 100;

//Simplex Noise.
float noiseScale = 40.0f;
float noiseOffsetX = 100.0f;
float noiseOffsetY = 100.0f;

//Cave.
int currentCave[caveHeight][caveWidth];
int nextCave[caveHeight][caveWidth];

//###Presets.
//Cave 1 (Random): FP: 50, BT: 4, DT: 4, DC: 75, BC: 100, Iter: 3.
//Cave 2 (Simplex): FP: 45, Scale: 40.
//Cave 3 (Simplex): FP: 45, Scale: 15.
//Cave 4 (Simplex): FP: 55, Scale: 20.
//Cave 5 (Simplex): FP: 50, Scale: 40. //Remove small occupied areas.

//Camera.
float cameraPanX = 120.0f; //Camera translation along the x-axis.
float cameraPanY = 90.0f; //Camera translation along the y-axis.
float cameraFOV = 150.0f; //Field of View.

//Colours.
float caveFaceColour[3] = {0.3f, 0.2f, 0.4f};
float caveDepthColour[3] = {0.35f, 0.25f, 0.45f};


/*@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@*/

//Using a chance value, outputs true if a random value is smaller than the chance.
bool thresholdRandom(int chance) {
	return rand() % 100 < chance;
}

//Generates the initial cave using Simplex noise.
void randomiseCave() {

	//Gets a random offset value for each direction for simplex noise.
	noiseOffsetX = rand() % 100000;
	noiseOffsetY = rand() % 100000;

	//Iterates through each cell in the cave.
	for (int y = 0; y < caveHeight; y++) { //For each column in the cave.
		for (int x = 0; x < caveWidth; x++) { //For each row in the cave.
			if (x < border || x > caveWidth - border - 1 || y < border || y > caveHeight - border - 1) {
				//Cave border.
				currentCave[y][x] = 0;
				nextCave[y][x] = 0;
			}
			else {
				//Maps each x,y coordinate to a scaled and offset coordinate.
				float mappedX = (float)x / caveWidth * noiseScale + noiseOffsetX;
				float mappedY = (float)y / caveHeight * noiseScale + noiseOffsetY;
				//Gets the noise value for the coordinate.
				float noise = SimplexNoise::noise(mappedX, mappedY);
				//Thresholds the noise value into either a free or occupied cell.
				float noiseThreshold = (fillPercentage / 50.0f) - 1.0f;
				currentCave[y][x] = (noise <= noiseThreshold) ? 0 : 1;
			}
		}
	}
}

int getNeighbourCount(int x, int y) {
	int count = 0;
	for (int i = x - 1; i <= x + 1; i++) {
		for (int j = y - 1; j <= y + 1; j++) {
			if (!(i == x && j == y) && currentCave[j][i] == 1) {
				count++;
			}
		}
	}
	return count;
}

//Performs one pass of a given ruleset of cellular automata to smooth the cave.
void smoothCave() {
	for (int y = border; y < caveHeight - border; y++) {
		for (int x = border; x < caveWidth - border; x++) {
			int neighbours = getNeighbourCount(x, y);
			if (neighbours > birthThreshold && thresholdRandom(birthChance)) {
				nextCave[y][x] = 1; //Cell is born.
			}
			else if (neighbours < deathThreshold && thresholdRandom(deathChance)) {
				nextCave[y][x] = 0; //Cell dies.
			}
			else {
				nextCave[y][x] = currentCave[y][x]; //Maintains the cell state.
			}
		}
	}
}

//Removes occupied areas with an area smaller than the given size. //###
void removeOccupiedAreas() {

}

//Adds a free area to the cave of given size and location. //###
void addFreeArea() {

}

/*@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@*/

//Displays the control text at the top-left of the window.
void displayControls() {
	float textColour[3] = {1.0f, 1.0f, 1.0f};
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 30, 0.15f, (char *)"Controls:", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 60, 0.15f, (char *)"'q' - Quit", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 90, 0.15f, (char *)"'['/']' - Zoom In/Out:", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 120, 0.15f, (char *)"'r' - Reset", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 150, 0.15f, (char *)"'n' - Scale Noise Up", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 180, 0.15f, (char *)"'m' - Scale Noise Down", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 210, 0.15f, (char *)"' ' - Smooth (1 iter.)", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 240, 0.15f, (char *)"'c' - Smooth (20 iter.)", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 270, 0.15f, (char *)"'k' - Decrease Fill Percentage", textColour);
	Draw::drawText(10, glutGet(GLUT_WINDOW_HEIGHT) - 300, 0.15f, (char *)"'l' - Increase Fill Percentage", textColour);
}

/*@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@~#~@*/

void idle() {
	//usleep(2500); // in microseconds
	//glutPostRedisplay();
}

void display() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//###
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(cameraFOV, (GLdouble)glutGet(GLUT_WINDOW_WIDTH) / (GLdouble)glutGet(GLUT_WINDOW_HEIGHT), 0.1f, 250.0f);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	//Eye Position, Reference Point, Up Vector.
	gluLookAt(cameraPanX, cameraPanY, 25, cameraPanX, cameraPanY, 0, 0, 1, 0);
	setLight(globalLight);

	//###DEBUG.
	cout << " + Camera Pan: (" << cameraPanX << ":" << cameraPanY << endl;
	cout << " + FOV: " << cameraFOV << endl;
	cout << " + Seed: " << noiseOffsetX << ":" << noiseOffsetY << " ~ Scale: " << noiseScale << endl;
	cout << " + Fill Percentage: " << fillPercentage << "%" << endl;
	cout << "[===================================================]" << endl;

	glPushMatrix();
	glEnable(GL_LIGHTING);

	float depth = -1.0f; //###

	//Draws Cave Background and Border.
	Draw::drawBackground(depth, caveWidth, caveHeight);
	Draw::drawBorder(depth, caveWidth, caveHeight);

	//Draw Cave.
	for (int i = 0; i < caveWidth; i++) { //For each cave column.
		for (int j = 0; j < caveHeight; j++) { //For each cave row.
			if (currentCave[j][i] == 0) { //If cell is free.
				glPushMatrix();

				//Translate to cell position.
				glTranslatef((float)i, (float)j, 0);
				glColor3fv(caveFaceColour);

				bool top = currentCave[j+1][i] == 1;
				bool left = currentCave[j][i-1] == 1;
				bool bottom = currentCave[j-1][i] == 1;
				bool right = currentCave[j][i+1] == 1;
				int occupiedNeighbourCount = currentCave[j+1][i] + currentCave[j][i-1] + currentCave[j-1][i] + currentCave[j][i+1];

				//Vertices.
				const float tl[3] = {-0.5f, 0.5f, 0};
				const float tl_d[3] = {-0.5f, 0.5f, depth};
				const float tr[3] = {0.5f, 0.5f, 0};
				const float tr_d[3] = {0.5f, 0.5f, depth};
				const float bl[3] = {-0.5f, -0.5f, 0};
				const float bl_d[3] = {-0.5f, -0.5f, depth};
				const float br[3] = {0.5f, -0.5f, 0};
				const float br_d[3] = {0.5f, -0.5f, depth};

				//Normals.
				const float nl[3] = {-1.0f, 0.0f, 0.0f};
				const float nr[3] = {1.0f, 0.0f, 0.0f};
				const float nt[3] = {0.0f, 1.0f, 0.0f};
				const float nb[3] = {0.0f, -1.0f, 0.0f};
				const float nf[3] = {0.0f, 0.0f, 1.0f};
				const float ntl[3] = {-1.0f, 1.0f, 0.0f};
				const float ntr[3] = {1.0f, 1.0f, 0.0f};
				const float nbl[3] = {-1.0f, -1.0f, 0.0f};
				const float nbr[3] = {1.0f, -1.0f, 0.0f};

				if (occupiedNeighbourCount == 2 && ((top && right) || (bottom && left))) {
					//Camera-Viewing polygon face.
					glColor3fv(caveFaceColour);
					glBegin(GL_POLYGON);
					glNormal3fv(nf); glVertex3fv(tl);
					glNormal3fv(nf); glVertex3fv(br);
					glNormal3fv(nf);
					top ? glVertex3fv(bl) : glVertex3fv(tr);
					glEnd();

					glColor3fv(caveDepthColour);
					//Depth face.
					glBegin(GL_QUAD_STRIP);
					top ? glNormal3fv(ntr) : glNormal3fv(nbl);
					glVertex3fv(tl);
					glVertex3fv(tl_d);
					glVertex3fv(br);
					glVertex3fv(br_d);
					glEnd();
				}
				else if (occupiedNeighbourCount == 2 && ((right && bottom) || (left && top))) {
					//Camera-Viewing polygon face.
					glColor3fv(caveFaceColour);
					glBegin(GL_POLYGON);
					glNormal3fv(nf); glVertex3fv(tr);
					glNormal3fv(nf); glVertex3fv(bl);
					glNormal3fv(nf);
					bottom ? glVertex3fv(tl) : glVertex3fv(br);
					glEnd();

					glColor3fv(caveDepthColour);
					//Depth face.
					glBegin(GL_QUAD_STRIP);
					bottom ? glNormal3fv(nbr) : glNormal3fv(ntl);
					glVertex3fv(tr);
					glVertex3fv(tr_d);
					glVertex3fv(bl);
					glVertex3fv(bl_d);
					glEnd();
				}
				else {
					//Camera-Viewing polygon face.
					glColor3fv(caveFaceColour);
					glBegin(GL_POLYGON);
					glNormal3fv(nf); glVertex3f(0.5f, 0.5f, 0);
					glNormal3fv(nf); glVertex3f(0.5f, -0.5f, 0);
					glNormal3fv(nf); glVertex3f(-0.5f, -0.5f, 0);
					glNormal3fv(nf); glVertex3f(-0.5f, 0.5f, 0);
					glEnd();

					glColor3fv(caveDepthColour);
					//Depth Face: Top.
					if (currentCave[j+1][i] == 1) {
						glBegin(GL_POLYGON);
						glNormal3fv(nt); glVertex3fv(tr);
						glNormal3fv(nt); glVertex3fv(tl);
						glNormal3fv(nt); glVertex3fv(tl_d);
						glNormal3fv(nt); glVertex3fv(tr_d);
						glEnd();
					}
					//Depth Face: Left.
					if (currentCave[j][i-1] == 1) {
						glBegin(GL_POLYGON);
						glNormal3fv(nl); glVertex3fv(bl);
						glNormal3fv(nl); glVertex3fv(tl);
						glNormal3fv(nl); glVertex3fv(tl_d);
						glNormal3fv(nl); glVertex3fv(bl_d);
						glEnd();
					}
					//Depth Face: Bottom.
					if (currentCave[j-1][i] == 1) {
						glBegin(GL_POLYGON);
						glNormal3fv(nb); glVertex3fv(br);
						glNormal3fv(nb);glVertex3fv(bl);
						glNormal3fv(nb);glVertex3fv(bl_d);
						glNormal3fv(nb);glVertex3fv(br_d);
						glEnd();
					}
					//Depth Face: Right.
					if (currentCave[j][i+1] == 1) {
						glBegin(GL_POLYGON);
						glNormal3fv(nr); glVertex3fv(br);
						glNormal3fv(nr); glVertex3fv(tr);
						glNormal3fv(nr); glVertex3fv(tr_d);
						glNormal3fv(nr); glVertex3fv(br_d);
						glEnd();
					}
				}

				glPopMatrix();
			}
		}
	}
	glPopMatrix();

	displayControls();
	glutSwapBuffers();
}

//Reshapes the view window.
void reshape(int w, int h) {
	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(cameraFOV, (GLdouble)w / (GLdouble)h, 0.1f, 250.0f);
}

//Mouse event functions.
void mouseInput(int button, int state, int x, int y) {
	switch (button) {
		//Scroll Up / Zoom Out.
		case 3: cameraFOV = (cameraFOV <= 25.0f) ? 25.0f : cameraFOV - 5.0f; break;
		//Scroll Down / Zoom In.
		case 4: cameraFOV = (cameraFOV >= 150.0f) ? 150.0f : cameraFOV + 5.0f; break;
	}

	//Reshapes the display.
	reshape(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
	glutPostRedisplay();
}

//Keyboard event functions.
void keyboardInput(unsigned char key, int, int) {
	switch (key) {
		//Exits the program.
		case 'q': exit(1); break;
		//Zoom Out.
		case ']': cameraFOV = (cameraFOV <= 25.0f) ? 25.0f : cameraFOV - 1.0f; cameraPanX *= cameraFOV; break; //###
		//Zoom In.
		case '[': cameraFOV = (cameraFOV >= 150.0f) ? 150.0f : cameraFOV + 1.0f; cameraPanX *= cameraFOV; break; //###
		//Randomise Cave. Also resets if already set.
		case 'r':
		case 'R': randomiseCave(); break;
		//###Noise Scale Up.
		case 'n': noiseScale += 1.0f; randomiseCave(); break;
		//###Noise Scale Down.
		case 'm': noiseScale -= 1.0f; randomiseCave(); break;
		//###Fill Percentage Decrease.
		case 'k': fillPercentage = (fillPercentage <= 0) ? 0 : fillPercentage - 1; randomiseCave(); break;
		//###Fill Percentage Increase.
		case 'l': fillPercentage = (fillPercentage >= 100) ? 100 : fillPercentage + 1; randomiseCave(); break;
		//Smooth 1 Iteration.
		case ' ':
			smoothCave();
			memcpy(currentCave, nextCave, sizeof(currentCave));
			break;
		//Smooth 20 Iterations.
		case 'c':
		  for (int i = 0; i < 20; i++) {
				smoothCave();
				memcpy(currentCave, nextCave, sizeof(currentCave));
			}
		  break;
	}
	glutPostRedisplay();
}

//Special Key event functions.
void specialKeyInput(int key, int x, int y) {
	switch (key) {
		//Pan Up.
		case GLUT_KEY_UP: cameraPanY += 0.2f * cameraFOV; break;
		//Pan Down.
		case GLUT_KEY_DOWN: cameraPanY -= 0.2f * cameraFOV; break;
		//Pan Left.
		case GLUT_KEY_LEFT: cameraPanX -= 0.2f * cameraFOV; break;
		//Pan Right.
		case GLUT_KEY_RIGHT: cameraPanX += 0.2f * cameraFOV; break;
	}
	glutPostRedisplay();
}

void init() {
	//Material.
	setMaterial(globalMaterial);
	glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

	//Light.
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);

	//Misc.
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_COLOR_MATERIAL);
	glShadeModel(GL_SMOOTH);
}

int main(int argc, char* argv[]) {

	//Random.
	srand(time(NULL));

	//Window Properties.
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	glutInitWindowSize(1000, 1000);
	glutInitWindowPosition(25, 25);
	glutCreateWindow("Cave Generation");

	//Event Functions.
	glutMouseFunc(mouseInput);
	glutKeyboardFunc(keyboardInput);
	glutSpecialFunc(specialKeyInput);
	glutReshapeFunc(reshape);
	glutDisplayFunc(display);
	glutIdleFunc(idle);

	//Cave Generation.
	randomiseCave();

	init();
	glutMainLoop();
	return 0;
}
