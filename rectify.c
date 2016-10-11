#include "lodepng.c"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>


// Argument struct for each thread
typedef struct {
    int tid;
    int width;
    int height;
    unsigned char *image;
    unsigned char *new_image;
    pthread_mutex_t *LOCK;
    int thread_num;
    char *output_filename;
    double *heights;
} thread_arg_t;


// Slice the image with almost equal amounts of height to process for each thread.
// If the hight does not actually get divided in equal chunks, distribute the remainer over
// Last threads so they do a bit more work. Ex a remainer of 3 height units will make
// last three threads take an extra unit.
void sliceHeights(double* heights_array, int image_height, int number_of_threads)
{
    int remainder = image_height % number_of_threads;
    int height_for_each_thread = ((int)(image_height/number_of_threads));
    
    for(int i=0; i<number_of_threads; i++)
    {
        // usual insertions for heights
        if((number_of_threads -i) > remainder)
        {
            *(heights_array+i) = height_for_each_thread;
        }
        // else the other threads ( modulo amount of threads) will ge extra one height unit from that modulo
        else if (remainder != 0 && (number_of_threads -i) <= remainder)
        {
            *(heights_array +i) = height_for_each_thread + 1;
        }
    }
    
}

// Actually does the rectification process by cehcking the RGB BIT values. (If lower than 127, set to 127)
void *do_image_rectification_process(void *arg)
{
    thread_arg_t *thread_arg = (thread_arg_t *) arg;
    unsigned char *input_image = thread_arg->image;
    unsigned char *new_image = thread_arg->new_image;
    int width = thread_arg -> width;
    int height = thread_arg -> height;
    pthread_mutex_t *LOCK = thread_arg -> LOCK;
    int thread_number = thread_arg -> thread_num;
    double* heights = thread_arg -> heights;
    char* output_filename = thread_arg -> output_filename;
    // process image
    unsigned char value;

    int endHeight =0;
    int startHeight = 0;
    
    for(int i = 0; i<thread_number; i++)
    {
        startHeight = startHeight + *(heights + i);
    }
    //int startHeight = thread_number * height;
    //int endHeight = (thread_number * height) + height;
    
    endHeight = startHeight + height;
    
    printf("I'm thread: %i and I work on start height %i till end height %i \n",thread_number, startHeight, endHeight-1);

    for (int i = startHeight; i < endHeight; i++) {
        for (int j = 0; j < width; j++) {
            
            value = input_image[4*width*i + 4*j];
            
            unsigned int R, G, B, A;
            
            R = input_image[4*width*i + 4*j + 0];
            G = input_image[4*width*i + 4*j + 1];
            B = input_image[4*width*i + 4*j + 2];
            A = input_image[4*width*i + 4*j + 3];
            
            // now one we got the R G B value, rectify them. If a value is less than 127, then set it to 127
            // otherwise, leave it as is.
            
            if(R<127)
            {
                R = 127;
            }
            if(G<127)
            {
                G = 127;
            }
            if(B<127)
            {
                B = 127;
            }
            
            //Setting tectified values
            new_image[4*width*i + 4*j + 0] = R; // R
            new_image[4*width*i + 4*j + 1] = G; // G
            new_image[4*width*i + 4*j + 2] = B; // B
            new_image[4*width*i + 4*j + 3] = A; // A
        }
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
    new_image = malloc(width * height * 4 * sizeof(unsigned char));
    
    //Each thread will have a height to deal with.
    double *heights = malloc(number_of_threads * sizeof(double));
    
    // SLICE THE IMAGE FOR HEIGHTS
    // Do processing for image height based on the # of threads.
    // We SLICE the image.
    // Ex:
    // If we have 2 threads, we divide the image in 2 (width same, but height in 2)
    // If we have 4 threads, we divide the image in 4 (width same, but height in 4)
    // If we have 8 threads, we divide the image in 8 (width same, but height in 8)
    // ETC
    sliceHeights(heights, height, number_of_threads);
    
    // At this point we will have an array of heights that each thread, depending on i will take. The heights are
    // divided all in almost equal space.
    
    // Now build argument structs for each thread
    printf("\n\n\n RECTIFICATION: EXECUTING WITH %i THREADS. \n\n\n",number_of_threads);
    for (int i=0; i< number_of_threads; i++)
    {
        // Create Argument Struct for each thread and then call new thread with the struct
        // First assign the mutex pointer in the args struct so that all threads will be able to access it
        // Then assign the input and output image arrays to each thread so they will be able to retrieve their
        // processing field in input image and write to the respective processing field of the output image.
        thread_args[i].LOCK = &LOCK;
        thread_args[i].image = image;
        thread_args[i].new_image = new_image;
        thread_args[i].width = width;           // all threads will have the same width of image to process
        thread_args[i].height = *(heights + i); // give it the amount of height to process
        thread_args[i].thread_num = i;
        thread_args[i].output_filename = output_filename;
        thread_args[i].heights = heights;
        
    }
    
    //Start time at thread exec start
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // Now call new threads with respective argument struct and the worker function (processing)
    for (int i = 0; i < number_of_threads; i++)
    {
        pthread_create(&thread_ids[i], NULL, do_image_rectification_process, (void *)&thread_args[i]);
    }
    
    printf("Joining threads! Waiting.. \n");
    for (int i = 0; i < number_of_threads; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }
    
    gettimeofday(&end, NULL);
    
    long interval = ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec));
    long converted_interval = interval/1000.0;
    printf("\n\nImage Processing took time : %ld ms\n", converted_interval);
    

    printf("Writing to file... \n");
    lodepng_encode32_file(output_filename, new_image, width, height);
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
    
    //Usage
    if(argc<4)
    {
        printf("INVALID NUMBER OF ARGUMENTS Usage: ./rectify <input PNG> <output PNG> <# of threads>\n");
        return 0;
    }
    
    char* input_filename = argv[1];
    char* output_filename = argv[2];
    int number_of_threads = atoi(argv[3]);
    
    if(number_of_threads < 1)
    {
        printf("ERROR: Can't have less than 1 thread.\n");
        return 1;
    }
    // Call main handler
    rectify(input_filename, output_filename, number_of_threads);
    
    return 0;
}
