#include "constant_filter.cuh"
//#include <cuda.h>
//#include <cuda_runtime_api.h>

#ifndef BYTES_PER_PIXEL
#define BYTES_PER_PIXEL 2
#endif
//This code largely inspired by http://madsravn.dk/posts/simple-image-processing-with-cuda/


//Kernel code, this runs on the GPU (device)
__global__ void pixel_constant_filter(u_char * pic_d, int16_t magnitude, int width, int height)
{
	int offset = blockIdx.x*gridDim.x +threadIdx.x; //This gives us how far we are into the u_char

	if(offset < width*height) //Because we needed an interger grid size, we will have a few threads that don't correspond to a location in the image.
	{
	//Each grayscale depth in the u_char * is represented by adjacent bytes in little endian order.
	// For this filter, we don't need to know where we are in the 2D sense since we are only doing a map operation. For gathers or stencils we will need to work on this.
	uint16_t current_value = pic_d[offset*BYTES_PER_PIXEL] | (pic_d[offset*BYTES_PER_PIXEL+1] << 8);
	//current_value += magnitude;
	pic_d[offset*BYTES_PER_PIXEL] =(u_char) current_value; //We want the LSB here
	pic_d[offset*BYTES_PER_PIXEL] =(u_char) (current_value << 8); //We want the MSB here

	}



}
u_char * apply_constant_filter(u_char * picture_in, int width, int height, int16_t filter_coeff)
{
	int pic_size = width*height*BYTES_PER_PIXEL;
	int block_length = 20;

	//u_char * picture_in; //Device (GPU) copy of picture in.
	u_char * picture_out = (u_char * )malloc(pic_size); //Create buffer for CPU memory output

	u_char * picture_device;
	cudaMalloc( (void **)&picture_device, pic_size);

	cudaMemcpy(picture_device, picture_in, pic_size, cudaMemcpyHostToDevice);
	//dim3 blockDims(block_length,block_length,1);
	dim3 blockDims(512,1,1);
	dim3 gridDims(ceil((float)width*height/blockDims.x),1,1);


	pixel_constant_filter<<<gridDims,blockDims>>>(picture_device, filter_coeff, width, height);
	cudaMemcpy(picture_out,picture_device,pic_size,cudaMemcpyDeviceToHost);

	cudaFree(picture_device);
	return picture_out;
}


