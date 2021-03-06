#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <stdio.h>
#include <math.h>
#include <GL/glut.h>
#include <algorithm>
#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <limits>
#include <tuple>
#include <queue>
#include "Cell.h"
#include "SenseCell.h"
#include "DroneConfig.h"
#include "MapCell.h"
#include "Drone.h"
using namespace std;

//Static Data Members.
float Drone::searchRadius = 10.0f; //Range of localised search.
float Drone::communicationRadius = 10.0f; //Range of inter-drone communication.
int Drone::communicationTimeBuffer = 25; //Minimum number of timesteps required between communication.

int Drone::caveWidth;
int Drone::caveHeight;
vector<vector<int>> Drone::cave;
int Drone::droneCount;

//Data Members.
int id; //Unique number for the drone.
string name; //Name of the drone.
float posX; //Current x position in the cave.
float posY; //Current y position in the cave.
float bearing; //0 -> Facing North.
bool complete; //Has finished exploration.
vector<vector<int>> internalMap; //Drone's identified cells of the cave.
map<int,int> frontierCells; //Free cells that are adjacent to unknowns.
vector<DroneConfig> pathList; //List of drone configurations for each timestep.
int currentTimestep; //Current timestep used to mark when frontiers were last identified.
pair<Cell,int> currentTarget; //Cell the drone is navigating to and the timestep in which it was identified.
vector<Cell> targetPath; //List of cells that head towards the current target.
vector<int> lastCommunication; //List of timesteps when last communicated with other drones.
vector<pair<float,float>> nearDrones; //List of nearby drones.
bool hasCommunicated; //Check to see if the drone has communicated in the current timestep.

//Statistics.
float totalTravelled; //Total distance travelled.
int freeCount; //Number of free cells identified.
int occupiedCount; //Number of occupied cells identified.
int commFreeCount; //Number of free cells recieved from inter-drone communication.
int commOccupiedCount; //Number of occupied cells recieved from inter-drone communication.


//Less than comparison function for two SenseCell objects.
bool operator <(const SenseCell& a, const SenseCell& b) {
  return a.range < b.range;
}

//Sets static cave properties.
void Drone::setParams(int _caveWidth, int _caveHeight, vector<vector<int>> _cave) {
  caveWidth = _caveWidth;
  caveHeight = _caveHeight;
  cave = _cave;
}

//Initalises the drone's starting position, name and internal map.
void Drone::init(int _id, float x, float y, string _name) {
  //Set given parameters.
  id = _id;
  posX = x;
  posY = y;
  name = _name;
  //Other data member defaults.
  bearing = 0.0f; //Facing east.
  complete = false;
  currentTimestep = 0;
  totalTravelled = 0;
  freeCount = 0;
  occupiedCount = 0;
  commFreeCount = 0;
  commOccupiedCount = 0;
  hasCommunicated = false;
  frontierCells.clear(); //Clears the frontier cells.
  pathList.clear();
  targetPath.clear();
  currentTarget = make_pair(Cell(-1,-1), -1); //Unreachable default target.

  //Last communication between drones.
  for (size_t i = 0; i < droneCount; i++) {
    lastCommunication.push_back(0);
  }

  //Sets the internal map to all unknowns.
  internalMap.clear();
  for (size_t i = 0; i < caveWidth; i++) {
    vector<int> column;
    for (size_t j = 0; j < caveHeight; j++) {
      column.push_back(Unknown);
    }
    internalMap.push_back(column);
  }

  //Initial sense and target.
  pair<vector<SenseCell>,vector<SenseCell>> buffers = sense();
  updateInternalMap(buffers.first, buffers.second);
  findFrontierCells(buffers.first, buffers.second);
  getNewTarget();
  recordConfiguration(); //Records the initial drone configuration.
}

//Sets the drone's current position in the cave.
void Drone::setPosition(float x, float y) {
  //Adds the distance difference to the total distance travelled.
  totalTravelled += getDistToDrone(x,y);
  //Calculates the bearing of the drone.
  bearing = atan2(x - posX,y - posY);
  //Sets the position.
  posX = x;
  posY = y;
}

//Models the sensing of the immediate local environment accounting for obstacles blocking sense view.
pair<vector<SenseCell>,vector<SenseCell>> Drone::sense() {

  vector<SenseCell> candidates; //List of candidate cells.
  vector<SenseCell> freeCells; //List of found free cells.
  vector<SenseCell> occupiedCells; //List of found occupied cells.
  vector<SenseCell> checkCells; //List of cells to check.

  //For each cell in the bounding box of the search range.
  //Discards Out-of-bounds cells (e.g. i = -1).
  for (size_t i = max(0, (int)floor(posX - searchRadius)); i <= min(caveWidth - 1, (int)ceil(posX + searchRadius)); i++) {
    for (size_t j = max(0, (int)floor(posY - searchRadius)); j <= min(caveHeight - 1, (int)ceil(posY + searchRadius)); j++) {
      //Allows only cells in the range.
      float range = getDistToDrone((float)i, (float)j);
      if (range > searchRadius) { continue; }
      //Push the candidate cell onto the vector.
      candidates.push_back(SenseCell(i,j,range));
    }
  }

  //Sorts the list of cells by distance to the drone in increasing order.
  sort(candidates.begin(), candidates.end());

  //Check to make sure you can't sense objects hidden behind something else.
  for (auto const& dest : candidates) {
    //If the cell range is 1 or less then immediately add it to the list.
    if (dest.range <= 1) {
      if (cave[dest.x][dest.y] == Free) {
        freeCells.push_back(dest);
      }
      else {
        occupiedCells.push_back(dest);
        checkCells.push_back(dest);
      }
      continue;
    }

    bool collisionDetected = false; //If an occupied cell blocks the path from the drone to the cell to be checked.

    //Obstacle in line of sight between drone position and destination cell check.
    for (auto const& occupyCheck : checkCells) {
      //Ignore if the cell to check is free.
      if (cave[occupyCheck.x][occupyCheck.y] == Free) { continue; }

      float xDiff = dest.x - posX;
      float yDiff = dest.y - posY;
      float tx0 = (occupyCheck.x - 0.5f - posX) / xDiff;
      float tx1 = (occupyCheck.x + 0.5f - posX) / xDiff;
      float ty0 = (occupyCheck.y - 0.5f - posY) / yDiff;
      float ty1 = (occupyCheck.y + 0.5f - posY) / yDiff;

      if (tx0 >= 0 && tx0 <= 1) {
        float yCheck = posY + tx0 * yDiff;
        if (yCheck >= occupyCheck.y - 0.5f && yCheck <= occupyCheck.y + 0.5f) {
          collisionDetected = true;
          break;
        }
      }
      if (tx1 >= 0 && tx1 <= 1) {
        float yCheck = posY + tx1 * yDiff;
        if (yCheck >= occupyCheck.y - 0.5f && yCheck <= occupyCheck.y + 0.5f) {
          collisionDetected = true;
          break;
        }
      }
      if (ty0 >= 0 && ty0 <= 1) {
        float xCheck = posX + ty0 * xDiff;
        if (xCheck >= occupyCheck.x - 0.5f && xCheck <= occupyCheck.x + 0.5f) {
          collisionDetected = true;
          break;
        }
      }
      if (ty1 >= 0 && ty0 <= 1) {
        float xCheck = posX + ty1 * xDiff;
        if (xCheck >= occupyCheck.x - 0.5f && xCheck <= occupyCheck.x + 0.5f) {
          collisionDetected = true;
          break;
        }
      }
    }

    //If no collision detected then the destination cell is in line of sight from the drone's position.
    if (!collisionDetected) {
      if (cave[dest.x][dest.y] == Free) {
        freeCells.push_back(dest);
      }
      else {
        occupiedCells.push_back(dest);
        checkCells.push_back(dest);
      }
    }
    else {
      checkCells.push_back(dest);
    }
  }

  return make_pair(freeCells, occupiedCells);
}

//Updates the internal map of the drone to include recently sensed free and occupied cells.
void Drone::updateInternalMap(vector<SenseCell> freeCellBuffer, vector<SenseCell> occupiedCellBuffer) {

  //Adds all free cells to the internal map.
  for (auto const& freeCell : freeCellBuffer) {
    int x = freeCell.x;
    int y = freeCell.y;
    if (internalMap[x][y] == Unknown) {
      internalMap[x][y] = Free;
      freeCount++;
    }
  }
  //Adds all occupied cells to the internal map.
  for (auto const& occupyCell : occupiedCellBuffer) {
    int x = occupyCell.x;
    int y = occupyCell.y;
    if (internalMap[x][y] == Unknown) {
      internalMap[x][y] = Occupied;
      occupiedCount++;
    }
  }
}

//Recalculates the set of frontier cells in the internal map.
void Drone::findFrontierCells(vector<SenseCell> freeCellBuffer, vector<SenseCell> occupiedCellBuffer) {

  //List of cells to check if they are frontier cels.
  vector<Cell> frontierCheck;

  //Iterates through each newly sensed free cell.
  //If the cell itself or a neighbour is a frontier cell, add it to the check list and set it to free.
  for (vector<SenseCell>::iterator freeCell = freeCellBuffer.begin(); freeCell != freeCellBuffer.end(); ++freeCell) {
    int x = freeCell->x;
    int y = freeCell->y;
    int i = y * caveWidth + x; //Dictionary key for the cell mapped into 1D.
    if (internalMap[x][y] == Frontier) {
      internalMap[x][y] = Free;
      frontierCells.erase(i);
    }
    if (x - 1 >= 0 && internalMap[x-1][y] == Frontier) {
      internalMap[x-1][y] = Free;
      frontierCells.erase(i-1);
      frontierCheck.push_back(Cell(x-1,y));
    }
    if (x + 1 < caveWidth && internalMap[x+1][y] == Frontier) {
      internalMap[x+1][y] = Free;
      frontierCells.erase(i+1);
      frontierCheck.push_back(Cell(x+1,y));
    }
    if (y - 1 >= 0 && internalMap[x][y-1] == Frontier) {
      internalMap[x][y-1] = Free;
      frontierCells.erase(i-caveWidth);
      frontierCheck.push_back(Cell(x,y-1));
    }
    if (y + 1 < caveHeight && internalMap[x][y+1] == Frontier) {
      internalMap[x][y+1] = Free;
      frontierCells.erase(i+caveWidth);
      frontierCheck.push_back(Cell(x,y+1));
    }
    frontierCheck.push_back(Cell(x,y));
  }

  //Iterates through each newly sensed occupied cell.
  //If a neighbour is a frontier cell, add it to the check list and set it to free.
  for (vector<SenseCell>::iterator occupyCell = occupiedCellBuffer.begin(); occupyCell != occupiedCellBuffer.end(); ++occupyCell) {
    int x = occupyCell->x;
    int y = occupyCell->y;
    int i = y * caveWidth + x; //Dictionary key for the cell mapped into 1D.
    if (x - 1 >= 0 && internalMap[x-1][y] == Frontier) {
      internalMap[x-1][y] = Free;
      frontierCells.erase(i-1);
      frontierCheck.push_back(Cell(x-1,y));
    }
    if (x + 1 < caveWidth && internalMap[x+1][y] == Frontier) {
      internalMap[x+1][y] = Free;
      frontierCells.erase(i+1);
      frontierCheck.push_back(Cell(x+1,y));
    }
    if (y - 1 >= 0 && internalMap[x][y-1] == Frontier) {
      internalMap[x][y-1] = Free;
      frontierCells.erase(i-caveWidth);
      frontierCheck.push_back(Cell(x,y-1));
    }
    if (y + 1 < caveHeight && internalMap[x][y+1] == Frontier) {
      internalMap[x][y+1] = Free;
      frontierCells.erase(i+caveWidth);
      frontierCheck.push_back(Cell(x,y+1));
    }
  }

  //For each cell to check if it neighbours an unknown cell set it as a Frontier cell and add it to the list of frontiers.
  for (vector<Cell>::iterator frontierCell = frontierCheck.begin(); frontierCell != frontierCheck.end(); ++frontierCell) {
    int x = frontierCell->x;
    int y = frontierCell->y;
    int i = y * caveWidth + x; //Dictionary key for the cell mapped into 1D.
    if (x - 1 >= 0 && internalMap[x-1][y] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = currentTimestep;
      continue;
    }
    if (x + 1 < caveWidth && internalMap[x+1][y] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = currentTimestep;
      continue;
    }
    if (y - 1 >= 0 && internalMap[x][y-1] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = currentTimestep;
      continue;
    }
    if (y + 1 < caveHeight && internalMap[x][y+1] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = currentTimestep;
      continue;
    }
  }
}

//Adds a drone position to the near drones vector.
void Drone::addNearDrone(float x, float y) {
  nearDrones.push_back(make_pair(x,y));
}

//For each nearby drone calculates the distance and bearing.
vector<pair<float,float>> Drone:: getNearDroneWeightMap() {

  vector<pair<float,float>> nearDroneWeight;

  //Iterates over each nearby drone.
  for (size_t i = 0; i < nearDrones.size(); i++) {
    float x = nearDrones[i].first;
    float y = nearDrones[i].second;

    //If drones have the same position, ignore.
    if (posX == x && posY == y) { continue; }

    float dist = getDistToDrone(x, y);
    //North 0, East PI/2, South PI, West 3PI/2.
    float theta = atan2(x - posX, y - posY);
    if (theta < 0.0f) {
      theta += M_PI * 2.0f;
    }
    nearDroneWeight.push_back(make_pair(theta, dist));
  }

  return nearDroneWeight;
}

//Gets the probability density of a given normal distribution.
float Drone::normalDistribution(float x, float mean, float std) {
  float var = pow(std, 2.0f);
  float coeff = 1.0f / (pow(2 * M_PI * var, 0.5f));
  float exponent = - pow(x - mean, 2.0f) / (2.0f * var);
  return coeff * exp(exponent);
}

//Finds the min/max distance and timestep for each frontier.
void Drone::getFrontierCellStats(float &minTs, float &maxTs, float &minDist, float &maxDist) {

  float distanceSum = 0.0f;

  //Gets the maximum timestep and minimum distance in the frontier cell list.
  for(auto& frontier : frontierCells) {
    //Updates minimum timestep if the frontier's timestep is lower.
    if (frontier.second < minTs) {
      minTs = frontier.second;
    }
    //Updates maximum timestep if the frontier's timestep is greater.
    if (frontier.second > maxTs) {
      maxTs = frontier.second;
    }
    //Updates the minimum distance if the frontier's distance is less.
    Cell frontierCell = intToCell(frontier.first);
    float frontierDistance = getDistToDrone(frontierCell);
    if (frontierDistance < minDist) {
      minDist = frontierDistance;
    }
    //Updates the maximum distance if the frontier's distance is greater.
    if (frontierDistance > maxDist) {
      maxDist = frontierDistance;
    }
    //Adds the distance to the frontier to the sum of distances.
    distanceSum += frontierDistance;
  }

  float distanceMean = distanceSum / frontierCells.size();
  float distanceSqSum = 0.0f;

  //Gets the variance of the distance.
  for(auto& frontier : frontierCells) {
    Cell frontierCell = intToCell(frontier.first);
    float frontierDistance = getDistToDrone(frontierCell);
    distanceSqSum += pow(frontierDistance - distanceMean, 2.0f);
  }

  float distanceStd = pow(distanceSqSum / frontierCells.size(), 0.5f);
}

//Finds the best frontier cell to navigate to.
pair<Cell,int> Drone::getBestFrontier(vector<pair<float,float>> nearDroneWeightMap) {

  if (nearDroneWeightMap.size() == 0) {
    return getLatestFrontier();
  }

  float minTs = numeric_limits<float>::max();
  float maxTs = 0.0f;
  float minDist = numeric_limits<float>::max();
  float maxDist = 0.0f;
  getFrontierCellStats(minTs, maxTs, minDist, maxDist);

  vector<tuple<Cell,int,float>> cellWeightVector;
  float cumulativeWeight = 0.0f;

  for(auto& frontier : frontierCells) {
    int frontierTs = frontier.second;

    Cell frontierCell = intToCell(frontier.first);
    float frontierDistance = getDistToDrone(frontierCell);
    float frontierBearing = atan2(frontierCell.x - posX, frontierCell.y - posY);
    if (frontierBearing < 0.0f) {
      frontierBearing += M_PI * 2.0f;
    }

    float bearingWeight = 1.0f;

    for (size_t i = 0; i < nearDroneWeightMap.size(); i++) {
      float bearingDiff = max(frontierBearing, nearDroneWeightMap[i].first) - min(frontierBearing, nearDroneWeightMap[i].first);
      float droneDist = nearDroneWeightMap[i].second;
      float droneDistWeight = pow(communicationRadius - droneDist, 2.0f);
      float pdf = 1.0f - normalDistribution(bearingDiff, 0.0f, M_PI / 8);
      bearingWeight *= pdf;
    }
    //Corrects weight if negative.
    if (bearingWeight < 0.0f) { bearingWeight = 0.0f; }


    float distRange = maxDist - minDist;
    float distWeight;
    if (distRange == 0) {
      distWeight = 1.0f;
    }
    else {
      distWeight = 1.0f - ((frontierDistance - minDist) / distRange);
    }

    float tsRange = (float)(maxTs - minTs);
    float tsWeight;
    if (tsRange == 0) {
      tsWeight = 1.0f;
    }
    else {
      tsWeight = (float)(frontierTs - minTs) / tsRange;
    }

    float weight = pow(distWeight, 1.0f) * pow(tsWeight, 2.0f) * pow(bearingWeight, 1.0f);

    cumulativeWeight += weight;
    cellWeightVector.push_back(make_tuple(frontierCell, frontierTs, cumulativeWeight));
  }


  float randWeight = static_cast<float>(rand())/(static_cast<float>(RAND_MAX/cumulativeWeight));

  for(auto& frontier : cellWeightVector) {
    Cell c; int ts; float w;
    tie(c,ts,w) = frontier;
    if (randWeight <= w) {
      return make_pair(c,ts);
    }
  }
}

//Gets the latest frontier cell added to the frontier list.
pair<Cell,int> Drone::getLatestFrontier() {

  int maxTimestep = 0;

  //Gets the maximum timestep in the frontier cell list.
  for(auto& frontier : frontierCells) {
    if (frontier.second > maxTimestep) {
      maxTimestep = frontier.second;
    }
  }

  float bestDistance = numeric_limits<float>::max();
  Cell bestFrontier;
  int ts;

  //Gets all the frontiers which have the maximum timestep.
  for(auto& frontier : frontierCells) {
    if (frontier.second == maxTimestep) {
      Cell a = intToCell(frontier.first);
      float dist = getDistToDrone(a);
      if (dist < bestDistance) {
        bestDistance = dist;
        bestFrontier = a;
        ts = frontier.second;
      }
    }
  }

  return make_pair(bestFrontier, ts);
}

//Gets the nearest frontier cell to the drone's current position.
pair<Cell,int> Drone::getNearestFrontier() {
  float bestDist = numeric_limits<float>::max();
  Cell bestFrontier;
  int timestep;

  //Iterates over every frontier cell to find the frontier nearest to the current drone position.
  for(auto& frontier : frontierCells) {
    int y = (int)frontier.first / caveWidth;
    int x = frontier.first % caveWidth;
    float dist = getDistToDrone(x, y);
    if (dist < bestDist) {
      bestDist = dist;
      bestFrontier = Cell(x,y);
      timestep = frontier.second;
    }
  }

  return make_pair(bestFrontier, timestep);
}

//Adds the drone's current configuration to the path.
void Drone::recordConfiguration() {
  pathList.push_back(DroneConfig(currentTimestep, posX, posY, bearing));
  currentTimestep++;
}

//Gets the closest cell to the drone's current position.
Cell Drone::getClosestCell(float x, float y) {

  float minDist = 100; //Abitrary large number.
  Cell closestCell;

  //Gets the nearest cell to given coordinates by searching the surrounding 2x2 block.
  for (int i = (int)floor(x); i <= (int)ceil(x); i++) {
    for (int j = (int)floor(y); j <= (int)ceil(y); j++) {
      float dist = pow(pow((float)i - x, 2.0f) + pow((float)j - y, 2.0f), 0.5f);
      if (dist < minDist) {
        minDist = dist;
        closestCell = Cell(i,j);
      }
    }
  }

  return closestCell;
}

//Uses A* and previously stored mapping of frontiers to find the path from the current position to the best frontier.
vector<Cell> Drone::getPathToTarget(pair<Cell,int> target) {
  int targetTimestep = target.second;
  Cell startPos = getClosestCell(posX, posY);
  vector<Cell> path = searchAStar(target.first, startPos); //Gets the path using A*.
  return path;
}

//Maps a cell to an integer value.
int Drone::cellToInt(Cell src) {
  return src.y * caveWidth + src.x;
}

//Maps an integer value to a cell in the cave.
Cell Drone::intToCell(int src) {
  int y = (int)src / caveWidth;
  int x = src % caveWidth;
  return Cell(x,y);
}

//Gets the distance from a given cell to the drone's current position.
float Drone::getDistToDrone(Cell dest) {
  return pow(pow(dest.x - posX, 2.0f) + pow(dest.y - posY, 2.0f), 0.5f);
}

//Gets the distance from a given cell to the drone's current position.
float Drone::getDistToDrone(int x, int y) {
  return pow(pow(x - posX, 2.0f) + pow(y - posY, 2.0f), 0.5f);
}

//Gets the Manhattan distance between two cells in the cave.
float Drone::getCellManhattanDist(Cell start, Cell end) {
  return abs(start.x - end.x) + abs(start.y - end.y);
}

//Gets the Euclidean distance between two cells in the cave.
float Drone::getCellEuclideanDist(Cell start, Cell end) {
  return pow(pow(start.x - end.x, 2.0f) + pow(start.y - end.y, 2.0f), 0.5f);
}

//Constructs the final path obtained from the A* algorithm.
vector<Cell> Drone::getAStarPath(map<int,int> previous, int current) {
  vector<Cell> totalPath;
  int cur = current;
  totalPath.push_back(intToCell(cur));
  while (previous.count(cur) > 0) {
    cur = previous[cur];
    totalPath.push_back(intToCell(cur));
  }
  return totalPath;
}

//Uses the A* algorithm to find a path between two cells.
vector<Cell> Drone::searchAStar(Cell start, Cell dest) {

  //If start cell is the same as the destination.
  if (start == dest) {
    vector<Cell> single;
    single.push_back(start);
    return single;
  }

  set<int> closedSet; //Set of evaluated cells.
  set<int> openSet; //Set of unevaluated cells.
  openSet.insert(cellToInt(start));

  map<int,int> previous;

  map<int,float> gScore;
  gScore[cellToInt(start)] = 0;

  map<int,float> fScore;
  fScore[cellToInt(start)] = getCellManhattanDist(start, dest);

  while (!openSet.empty()) {
    Cell current;
    float minScore = numeric_limits<float>::max();

    //Gets the cell with the smallest fScore.
    for (auto const& x : fScore) {
      if (openSet.count(x.first) > 0 && x.second < minScore) {
        minScore = x.second;
        current = intToCell(x.first);
      }
    }

    int currentI = cellToInt(current);
    if (current == dest) {
      return getAStarPath(previous, currentI);
    }

    set<int>::iterator currentIt = openSet.find(currentI);
    openSet.erase(currentIt);
    closedSet.insert(currentI);

    //List of adjacent free/frontier cells to the current cell.
    vector<Cell> neighbours;
    int x = current.x;
    int y = current.y;
    bool left = x - 1 >= 0 && (internalMap[x-1][y] == Free || internalMap[x-1][y] == Frontier);
    bool right = x + 1 < caveWidth && (internalMap[x+1][y] == Free || internalMap[x+1][y] == Frontier);
    bool bottom = y - 1 >= 0 && (internalMap[x][y-1] == Free || internalMap[x][y-1] == Frontier);
    bool top = y + 1 < caveHeight && (internalMap[x][y+1] == Free || internalMap[x][y+1] == Frontier);
    bool bottomleft = bottom && left && (internalMap[x-1][y-1] == Free || internalMap[x-1][y-1] == Frontier);
    bool bottomright = bottom && right && (internalMap[x+1][y-1] == Free || internalMap[x+1][y-1] == Frontier);
    bool topleft = top && left && (internalMap[x-1][y+1] == Free || internalMap[x-1][y+1] == Frontier);
    bool topright = top && right && (internalMap[x+1][y+1] == Free || internalMap[x+1][y+1] == Frontier);

    //Left Neighbour.
    if (left) { neighbours.push_back(Cell(x-1,y)); }
    //Right Neighbour.
    if (right) { neighbours.push_back(Cell(x+1,y)); }
    //Bottom Neighbour.
    if (bottom) { neighbours.push_back(Cell(x,y-1)); }
    //Top Neighbour.
    if (top) { neighbours.push_back(Cell(x,y+1)); }
    //Bottom-Left Neighbour.
    if (bottomleft) { neighbours.push_back(Cell(x-1,y-1)); }
    //Bottom-Right Neighbour.
    if (bottomright) { neighbours.push_back(Cell(x+1,y-1)); }
    //Top-Left Neighbour.
    if (topleft) { neighbours.push_back(Cell(x-1,y+1)); }
    //Top-Right Neighbour.
    if (topright) { neighbours.push_back(Cell(x+1,y+1)); }

    //Iterate over each neighbour.
    for (auto const& neighbour : neighbours) {
      int neighbourI = cellToInt(neighbour);

      //Skip neighbour cell if it has previously been evaluated.
      if (closedSet.count(neighbourI) > 0) { continue; }

      int midDist = gScore[currentI] + getCellEuclideanDist(current, neighbour);

      //New node discovered.
      if (openSet.count(neighbourI) == 0) {
        openSet.insert(neighbourI);
      }
      else if (midDist >= gScore[neighbourI]) {
        continue;
      }

      previous[neighbourI] = currentI;
      gScore[neighbourI] = midDist;
      fScore[neighbourI] = gScore[neighbourI] + getCellManhattanDist(neighbour, dest);
    }
  }

  vector<Cell> null;
  return null;
}

//Processes the drone's movement, sensing, frontier identification and selection for one timestep.
void Drone::process() {

  //Delay for each consecutive drone to allow spacing.
  if (currentTimestep - 1 <= id) {
    recordConfiguration();
    return;
  }

  //If no frontiers to explore, then search is complete.
  if (frontierCells.size() == 0) {
    complete = true;
    outputStatistics();
    return;
  }


  //If current target has been discovered.
  if (internalMap[currentTarget.first.x][currentTarget.first.y] != Frontier || hasCommunicated) {
    getNewTarget();
    hasCommunicated = false;
  }
  else {
    setPosition(targetPath.front().x, targetPath.front().y);
    targetPath.erase(targetPath.begin()); //Removes the first cell in the target path.
  }

  pair<vector<SenseCell>,vector<SenseCell>> buffers = sense();
  updateInternalMap(buffers.first, buffers.second);
  findFrontierCells(buffers.first, buffers.second);
  recordConfiguration();
  nearDrones.clear();
}

//Gets a new target from the list of frontier cells accounting for nearby drones and known mapping.
void Drone::getNewTarget() {

  vector<pair<float,float>> nearDroneWeightMap = getNearDroneWeightMap();
  bool newTargetFound = false;

  while (!newTargetFound) {
    currentTarget = getBestFrontier(nearDroneWeightMap);
    targetPath = getPathToTarget(currentTarget);
    //Target unreachable.
    if (targetPath.size() == 0) {
      frontierCells.erase(currentTarget.first.y * caveWidth + currentTarget.first.x);
      internalMap[currentTarget.first.x][currentTarget.first.y] = Free;
    }
    else {
      newTargetFound = true;
    }
  }

}

//Outputs drone statistics to the console.
void Drone::outputStatistics() {
  cout << "[" << name << "] - Search Complete." << endl;
  cout << "[" << name << "] - Distance Travelled: (" << totalTravelled << ") - Timesteps: (" << currentTimestep << ")" << endl;
  cout << "[" << name << "] - Free Cells: (" << freeCount << ") - Occupied Cells: (" << occupiedCount << ")" << endl;
}

//Check to see if enough time has elapsed to allow inter-drone communication.
bool Drone::allowCommunication(int x) {
  return (currentTimestep >= lastCommunication[x] + communicationTimeBuffer);
}

//Merges the drone's internal map with another drone's map.
void Drone::combineMaps(vector<vector<int>> referenceMap, map<int,int> referenceFrontierMap, int droneID) {

  hasCommunicated = true; //Communication in the current timestep.

  vector<Cell> frontierCheck; //List of cells to check if they are frontiers.
  lastCommunication[droneID] = currentTimestep;

  //Updates internal map with the given reference map.
  for (size_t i = 0; i < caveWidth; i++) {
    for (size_t j = 0; j < caveHeight; j++) {

      if (referenceMap[i][j] == Unknown) {
        continue;
      }
      else if (referenceMap[i][j] == Occupied && internalMap[i][j] == Unknown) {
        //Update unknown cell to occupied.
        internalMap[i][j] = Occupied;
        occupiedCount++;
        commOccupiedCount++;
        //Adds the neighbouring cells to the list to be checked.
        if (i - 1 >= 0 && internalMap[i-1][j] == Frontier) { frontierCheck.push_back(Cell(i-1,j)); }
        if (i + 1 < caveWidth && internalMap[i+1][j] == Frontier) { frontierCheck.push_back(Cell(i+1,j)); }
        if (j - 1 >= 0 && internalMap[i][j-1] == Frontier) { frontierCheck.push_back(Cell(i,j-1)); }
        if (j + 1 < caveHeight && internalMap[i][j+1] == Frontier) { frontierCheck.push_back(Cell(i,j+1)); }
      }
      else if (referenceMap[i][j] == Free && internalMap[i][j] != Free) {
        //Update free cell.
        if (internalMap[i][j] == Unknown) {
          freeCount++;
          commFreeCount++;
        }
        else if (internalMap[i][j] == Frontier) {
          frontierCells.erase(j * caveWidth + i); //Removes the frontier from the frontier cell list.
        }
        internalMap[i][j] = Free;
        //Adds the neighbouring cells to the list to be checked.
        if (i - 1 >= 0 && internalMap[i-1][j] == Frontier) { frontierCheck.push_back(Cell(i-1,j)); }
        if (i + 1 < caveWidth && internalMap[i+1][j] == Frontier) { frontierCheck.push_back(Cell(i+1,j)); }
        if (j - 1 >= 0 && internalMap[i][j-1] == Frontier) { frontierCheck.push_back(Cell(i,j-1)); }
        if (j + 1 < caveHeight && internalMap[i][j+1] == Frontier) { frontierCheck.push_back(Cell(i,j+1)); }
      }
      else if (referenceMap[i][j] == Frontier && internalMap[i][j] == Unknown) {
        //Update frontier cell.
        freeCount++;
        commFreeCount++;
        internalMap[i][j] = Free;
        frontierCheck.push_back(Cell(i,j));
      }
    }
  }

  //Checks each cell in the frontier check vector to see if it is a frontier.
  for (auto& cell : frontierCheck) {
    int x = cell.x;
    int y = cell.y;
    int i = y * caveWidth + x; //Dictionary key for the cell mapped into 1D.
    int ts = referenceFrontierMap[i];

    if (x - 1 >= 0 && internalMap[x-1][y] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = 0;
    }
    else if (x + 1 < caveWidth && internalMap[x+1][y] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = 0;
    }
    else if (y - 1 >= 0 && internalMap[x][y-1] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = 0;
    }
    else if (y + 1 < caveHeight && internalMap[x][y+1] == Unknown) {
      internalMap[x][y] = Frontier;
      frontierCells[i] = 0;
    }
  }
}

//Outputs drone statistics as a vector string.
vector<string> Drone::getStatistics() {
  vector<string> output;
  output.push_back(to_string((int)totalTravelled));
  output.push_back(to_string(freeCount));
  output.push_back(to_string(occupiedCount));
  output.push_back(to_string(commFreeCount));
  output.push_back(to_string(commOccupiedCount));
  output.push_back(to_string(complete));
  return output;
}
