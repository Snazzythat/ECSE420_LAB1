#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>

typedef struct {
    int tid;
    int blocksToProcess;
    unsigned char *image;
    unsigned char *new_image;
    pthread_mutex_t *LOCK;
    int thread_num;
    char *output_filename;
    double *blocks;
	int width;
} thread_arg_t;


// Slice the image with almost equal amounts of blocks to process for each thread.
// If the blocks do not actually get divided in equal chunks, distribute the remainer over
// Last threads so they do a bit more work. Ex a remainer of 3 blocks units will make
// last three threads take an extra unit.
void sliceBlocks(double* blocks_array, int image_height, int image_width, int number_of_threads)
{
	int totalBlocks = (image_height * image_width)/4;
    int remainder = totalBlocks % number_of_threads;
    int blocks_for_each_thread = ((int)(totalBlocks/number_of_threads));
    
    for(int i=0; i<number_of_threads; i++)
    {
        // usual insertions for blocks
        if((number_of_threads -i) > remainder)
        {
            *(blocks_array+i) = blocks_for_each_thread;
        }
        // else the other threads ( modulo amount of threads) will ge extra one block unit from that modulo
        else if (remainder != 0 && (number_of_threads -i) <= remainder)
        {
            *(blocks_array +i) = blocks_for_each_thread + 1;
        }
    }
}

// Actually does the rectification process by cehcking the RGB BIT values. (If lower than 127, set to 127)
void *do_image_pooling_process(void *arg)
{
    thread_arg_t *thread_arg = (thread_arg_t *) arg;
    unsigned char *input_image = thread_arg->image;
    unsigned char *new_image = thread_arg->new_image;
	int widthInitialImage = thread_arg -> width;
    int blocksToProcess = thread_arg -> blocksToProcess;
    pthread_mutex_t *LOCK = thread_arg -> LOCK;
    int thread_number = thread_arg -> thread_num;
    double* blocks = thread_arg -> blocks;
    char* output_filename = thread_arg -> output_filename;

    int endBlock =0;
    int startBlock = 0;
    
    for(int i = 0; i<thread_number; i++)
    {
        startBlock = startBlock + *(blocks + i);
    }
    
    endBlock = startBlock + blocksToProcess;
	
	int blocksPerLine = widthInitialImage/2;
    
	int startLine = ((int)(startBlock / blocksPerLine));
	int startColumn = startBlock - (startLine*blocksPerLine);
	
	int endLine = ((int)((endBlock-1) / blocksPerLine));
	int endColumn = (endBlock-1) - (endLine*blocksPerLine);
	
    printf("I'm thread: %i and I work on start block %i (%ix%i) till end block %i (%ix%i).\n",thread_number, startBlock, startLine, startColumn, endBlock-1, endLine, endColumn);

    for (int i = startBlock; i < endBlock; i++) {
		//Each block contains 4 pixels. The block's coordinates point to the left uppermost pixel.
		int blockLine = ((int)(i / blocksPerLine));
		int blockColumn = (i - (blockLine*blocksPerLine));
		
		int pixelLine = blockLine * 2;
		int pixelColumn = blockColumn * 2;
		
		unsigned int maxR=0;
		for(int j = 0; j < 2; j++){
			for(int k = 0; k < 2; k++){
				int elementsInUpperLines = (pixelLine+j) * widthInitialImage;
				unsigned int valueR = input_image[(4*elementsInUpperLines + 4*(pixelColumn+k) + 0)];
				if(maxR < valueR){
					maxR = valueR;
				}
			}
		}

		unsigned int maxG=0;
		for(int j = 0; j < 2; j++){
			for(int k = 0; k < 2; k++){
				int elementsInUpperLines = (pixelLine+j) * widthInitialImage;
				unsigned int valueG = input_image[(4*elementsInUpperLines + 4*(pixelColumn+k) + 1)];
				if(maxG < valueG){
					maxG = valueG;
				}
			}
		}
		
		unsigned int maxB=0;
		for(int j = 0; j < 2; j++){
			for(int k = 0; k < 2; k++){
				int elementsInUpperLines = (pixelLine+j) * widthInitialImage;
				unsigned int valueB = input_image[(4*elementsInUpperLines + 4*(pixelColumn+k) + 2)];
				if(maxB < valueB){
					maxB = valueB;
				}
			}
		}
		
		unsigned int maxA=0;
		for(int j = 0; j < 2; j++){
			for(int k = 0; k < 2; k++){
				int elementsInUpperLines = (pixelLine+j) * widthInitialImage;
				unsigned int valueA = input_image[(4*elementsInUpperLines + 4*(pixelColumn+k) + 3)];
				if(maxA < valueA){
					maxA = valueA;
				}
			}
		}
		
		//Setting pooled values
		new_image[4*blockLine*blocksPerLine + 4*blockColumn + 0] = maxR; // R
		new_image[4*blockLine*blocksPerLine + 4*blockColumn + 1] = maxG; // G
		new_image[4*blockLine*blocksPerLine + 4*blockColumn + 2] = maxB; // B
		new_image[4*blockLine*blocksPerLine + 4*blockColumn + 3] = maxA; // A
   }

	pthread_exit(NULL);
}

void rectify(char* input_filename, char* output_filename, int number_of_threads)
{
    unsigned error;
    unsigned char *image, *new_image;
    unsigned width, height;
    
    //---Can have: 1, 2, 4, 8, 16, 32 p threads executing the task
    //--Initialize mutex for threads to be able to write to file each at their own time.
    //--Otherwise, dead lock happens.
    pthread_mutex_t LOCK;
    pthread_mutex_init(&LOCK, NULL);
    
    // Define size for thread IDs
    pthread_t *thread_ids = malloc(number_of_threads * sizeof(pthread_t));
    // Define size for thread ARGGS
    thread_arg_t *thread_args = malloc(number_of_threads * sizeof(thread_arg_t));
    
    //Load Image first
    error = lodepng_decode32_file(&image, &width, &height, input_filename);
    if(error) printf("error %u: %s\n", error, lodepng_error_text(error));
    new_image = malloc((width/2) * (height/2) * 4 * sizeof(unsigned char));
    
    //Each thread will have a number of blocks to deal with.
    double *blocks = malloc(number_of_threads * sizeof(double));
    
    // SLICE THE IMAGE FOR BLOCKS
    // Do processing for image blocks based on the # of threads.
    // We divide the image into 2x2 blocks.
    // Ex:
    // If we have 2 threads, we divide the image in 2 sets of 2x2 blocks
    // If we have 4 threads, we divide the image in 4 sets of 2x2 blocks
    // If we have 8 threads, we divide the image in 8 sets of 2x2 blocks
    // ETC
    sliceBlocks(blocks, height, width, number_of_threads);
    
    // At this point we will have an array of blocks that each thread, depending on i will take. The blocks are
    // divided all in almost equal space.
    
    // Now build argument structs for each thread
    printf("\n\n\n RECTIFICATION: EXECUTING WITH %i THREADS. \n\n\n",number_of_threads);
    for (int i=0; i< number_of_threads; i++)
    {
        // Create Argument Struct for each thread and then call new thread with the struct
        // First assign the mutex pointer in the args struct so that all threads will be able to access it
        // Then assign the input and output image arrays to each thread so they will be able to retrieve their
        // processing field in input image and write to the respective processing field of the output image.
        
		thread_args[i].blocksToProcess = *(blocks + i); // give it the amount of blocks to process
        thread_args[i].image = image;
        thread_args[i].new_image = new_image;
		thread_args[i].LOCK = &LOCK;
        thread_args[i].thread_num = i;
        thread_args[i].output_filename = output_filename;
        thread_args[i].blocks = blocks;
		thread_args[i].width = width;
    }
    
    //Start time at thread exec start
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Now call new threads with respective argument struct and the worker function (processing)
    for (int i = 0; i < number_of_threads; i++)
    {
        pthread_create(&thread_ids[i], NULL, do_image_pooling_process, (void *)&thread_args[i]);
    }
    
    printf("Joining threads! Waiting.. \n");
    for (int i = 0; i < number_of_threads; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }
    
    gettimeofday(&end, NULL);
    
    long interval = ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec));
    float converted_interval = (float)interval/1000.0;
    printf("\n\nImage Processing took time : %.2f ms\n", converted_interval);
    

    printf("Writing to file... \n");
    lodepng_encode32_file(output_filename, new_image, width/2, height/2);
    printf("Writing to file...DONE \n");
    
    printf("Freeing Ressources..\n");
    free(thread_ids);
    free(thread_args);
    free(image);
    free(new_image);
    printf("Freed!!!!\n");
}

int main(int argc, char *argv[])
{
    char* input_filename = argv[1];
    char* output_filename = argv[2];
    int number_of_threads = atoi(argv[3]);
    
    if(number_of_threads < 1)
    {
        printf("ERROR: Can't have less than 1 thread.");
        return 1;
    }
    rectify(input_filename, output_filename, number_of_threads);
    
    return 0;
}
