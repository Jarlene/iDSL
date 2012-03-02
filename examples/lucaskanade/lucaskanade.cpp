#include "../util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "svdcmp.h"

bool near(double a, double b){
  return abs(1.f-(a/b)) < 0.001f;
}

// calculate the pseudo inverse in place
// if singular return a matrix of zeros
void pinv2by2(float *A){

  if(A[0]*A[3]-A[1]*A[2] == 0){

    printf("Singular\n");
    A[0]=0;
    A[1]=0;
    A[2]=0;
    A[3]=0;

    return;
  }

  double **u = dmatrix(1,2,1,2);
  double **v = dmatrix(1,2,1,2);
  u[1][1]=A[0];
  u[1][2]=A[1];
  u[2][1]=A[2];
  u[2][2]=A[3];
  double w[3];
  svdcmp(u, 2, 2, w, v);

  // A_pinv = V * w_pinv * U_transpose
  // w_pinv is just w with the entries inverted (it's a diagonal square matrix)
  double temp[4];
  temp[0] = v[1][1] * (1.f/w[1]); //temp = V*w_pinv
  temp[2] = v[2][1] * (1.f/w[1]);
  temp[1] = v[1][2] * (1.f/w[2]);
  temp[3] = v[2][2] * (1.f/w[2]);

  double R[4];

  R[0] = temp[0]*u[1][1] + temp[1]*u[1][2];
  R[2] = temp[2]*u[1][1] + temp[3]*u[1][2];
  R[1] = temp[0]*u[2][1] + temp[1]*u[2][2];
  R[3] = temp[2]*u[2][1] + temp[3]*u[2][2];

  {  // varify correctness
    double S[4];
    S[0] = R[0]*A[0]+R[1]*A[2]; // = A_pseudo * A
    S[1] = R[0]*A[1]+R[1]*A[3];
    S[2] = R[2]*A[0]+R[3]*A[2];
    S[3] = R[2]*A[1]+R[3]*A[3];

    double AApA[4];
    AApA[0] = A[0]*S[0]+A[1]*S[2];
    AApA[1] = A[0]*S[1]+A[1]*S[3];
    AApA[2] = A[2]*S[0]+A[3]*S[2];
    AApA[3] = A[2]*S[1]+A[3]*S[3];

    // A * A_pseudo * A == A
    if(!(near(AApA[0],A[0]) && 
	 near(AApA[1],A[1]) && 
	 near(AApA[2],A[2]) && 
	 near(AApA[3],A[3]))){
      
      printf("%f %f %f %f\n",A[0],A[1],A[2],A[3]);
      printf("pinv %f %f %f %f\n",R[0],R[1],R[2],R[3]);
      printf("%f %f %f %f\n",S[0],S[1],S[2],S[3]);
      printf("AApA %f %f %f %f\n",AApA[0],AApA[1],AApA[2],AApA[3]);
      printf("w %f %f\n",w[1],w[2]);
      assert(false);
    }
  }

  A[0] = R[0];
  A[1] = R[1];
  A[2] = R[2];
  A[3] = R[3];

  free_dmatrix(u,1,2,1,2);
  free_dmatrix(v,1,2,1,2);
}

// see here for ref: http://www.cs.ucf.edu/~mikel/Research/Optical_Flow.htm
// output should have 3 channels (size width*height*3)
void lucaskanade(
  int width, 
  int height, 
  int windowRadius, 
  int iterations,
  int pyramidLevels,
  bool weighted,
  unsigned char* frame1, 
  unsigned char* frame2, 
  unsigned char* out){

  int border = windowRadius;

  unsigned char **frame1Pyramid = NULL;
  unsigned char **frame2Pyramid = NULL;

  buildPyramid(width,height,pyramidLevels,frame1,frame1Pyramid);
  buildPyramid(width,height,pyramidLevels,frame2,frame2Pyramid);

  // zero out vectors array. 128 = 0 in our stupid fixed precision format
  for(int x = 0; x<width; x++){
    for(int y = 0; y<height; y++){
      for(int c = 0; c<3; c++){

	if(x >= border &&
	   x < width-border &&
	   y >= border &&
	   y < height-border &&
	   c!=2){
	  out[(y*width+x)*3+c]=128;
	}else{
	  out[(y*width+x)*3+c]=0;
	}
      }
    }
  }

  // calculate stuff that we will use every iteration
  // such as derivatives, matrix A^-1 of image gradients, weights.
  // we're calling frame1 F and frame2 G, as in the original LK paper
  float *Fx = new float[width*height];
  float *Fy = new float[width*height];
  float *A = new float[width*height*4];
  float *W = new float[width*height];

  for(int x = border; x < width - border; x++){
    for(int y = border; y < height - border; y++){
      // calculate derivatives
      Fx[y*width+x] = (float(frame1[y*width+x+1])-float(frame1[y*width+x-1]))/2.f;
      Fy[y*width+x] = (float(frame1[(y+1)*width+x])-float(frame1[(y-1)*width+x]))/2.f;

      float Gx = (float(frame2[y*width+x+1])-float(frame2[y*width+x-1]))/2.f;
      float Gy = (float(frame2[(y+1)*width+x])-float(frame2[(y-1)*width+x]))/2.f;

      // calculate weight W
      float w = sqrt(pow(Gx-Fx[y*width+x],2)+pow(Gy-Fy[y*width+x],2));
      if(w!=0.f){w = 1.f/w;}
      if(!weighted){w = 1.f;}
      W[y*width+x] = w;
    }
  }

  float Atemp[4]; // row major 2x2
  // calculate A^-1. notice, can't combine this with loop above
  // b/c it requires all values to already be calculated in diff window
  for(int x = border; x < width - border; x++){
    for(int y = border; y < height - border; y++){

      Atemp[0] = 0.f;
      Atemp[1] = 0.f;
      Atemp[2] = 0.f;
      Atemp[3] = 0.f;

      for( int wx = -windowRadius; wx <= windowRadius; wx++){
	for( int wy = -windowRadius; wy <= windowRadius; wy++){
	  int windex = (y+wy)*width+x+wx;

	  float dx = Fx[windex];
	  float dy = Fy[windex];
	    
	  Atemp[0] = Atemp[0] + dx*dx*W[windex];
	  Atemp[1] = Atemp[1] + dx*dy*W[windex];
	  Atemp[2] = Atemp[2] + dx*dy*W[windex];
	  Atemp[3] = Atemp[3] + dy*dy*W[windex];
	  
	}
      }

      pinv2by2(Atemp);

      A[(y*width+x)*4+0]=Atemp[0];
      A[(y*width+x)*4+1]=Atemp[1];
      A[(y*width+x)*4+2]=Atemp[2];
      A[(y*width+x)*4+3]=Atemp[3];
    }
  }

  // do LK calculation
  /* Notice: instead of iterating the same # of times for each pixel,
     we could instead iterate a different # of times for each pixel 
     (until the error < epsilon for ex). This would prob make for better
     results, but wouldn't be parallelizable
  */
  for(int i = 0; i<iterations; i++){
    for(int x = border; x < width - border; x++){
      printf("\b\b\b\b\b\b\b\b\b\b%03d / %03d\n",x,width-border);
      
      for(int y = border; y < height - border; y++){

	float b[2]; 
	b[0] = 0.f;
	b[1] = 0.f;

	float hx = (float(out[3*(y*width+x)])-128.f)/10.f;
	float hy = (float(out[3*(y*width+x)+1])-128.f)/10.f;

	// loop over search window
	for( int wx = -windowRadius; wx <= windowRadius; wx++){
	  for( int wy = -windowRadius; wy <= windowRadius; wy++){
	    int windex = (y+wy)*width+x+wx;

	    float dx = Fx[windex];
	    float dy = Fy[windex];
	    float F = frame1[windex];
	    float G = sampleBilinear( width, height, x+wx+hx, y+wy+hy, frame2);
	    
	    b[0] = b[0] + dx*(G-F)*W[windex];
	    b[1] = b[1] + dy*(G-F)*W[windex];
	  }
	}
	
	float outX = A[(y*width+x)*4+0]*(-b[0])+A[(y*width+x)*4+1]*(-b[1]); // result = Ainv * (-b)
	float outY = A[(y*width+x)*4+2]*(-b[0])+A[(y*width+x)*4+3]*(-b[1]);
	out[3*(y*width+x)]=128+(outX+hx)*10;
	out[3*(y*width+x)+1]=128+(outY+hy)*10;
      }
    }
  }
}

int main(int argc, char **argv){

  if(argc!=7 && argc!=8){
    printf("Usage: lucaskanade frame1.type frame2.type out.bmp searchWindowRadius iterations pyramidLevels [-w]\n");
    printf("add -w on the end to use the algorithm with weights\n");
    return 1;
  }

  int width, height, channels;
  int width2, height2, channels2;
  unsigned char *data;
  unsigned char *data2;

  loadImage(argv[1], &width, &height, &channels, &data);
  loadImage(argv[2], &width2, &height2, &channels2, &data2);

  assert(width==width2);
  assert(height==height2);
  assert(channels==channels2);
  assert(channels==3);

  unsigned char *frame1 = new unsigned char[width*height];
  unsigned char *frame2 = new unsigned char[width*height];
  unsigned char *out = new unsigned char[width*height*channels];

  toGrayscale(width,height,data,frame1);
  toGrayscale(width,height,data2,frame2);

  saveImage("frame1gray.bmp", width, height, 1, frame1);
  saveImage("frame2gray.bmp", width, height, 1, frame2);

  lucaskanade(width,height,atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),argc==8,frame1,frame2,out);

  saveImage(argv[3], width, height, channels, out);

  delete[] frame1;
  delete[] frame2;
  delete[] out;

  return 0;
}
