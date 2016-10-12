#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "wm.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

// Thread args struct
typedef struct
{
    unsigned char *input_image;
    unsigned char *output_image;
    unsigned image_width;
    unsigned image_height;
    unsigned id;
    unsigned blks;
    unsigned blks_off;
} thread_arg_t;

// Main worker fucntion for each thread
void *do_convolution_process(void *arg)
{
    thread_arg_t *thread_arg = (thread_arg_t *) arg;
    unsigned char *input_image = thread_arg->input_image;
    unsigned width = thread_arg->image_width;
    unsigned char *output_image = thread_arg->output_image;
    int thread_blks = thread_arg->blks;
    unsigned blocks_offset = thread_arg->blks_off;
    signed result_conv;
    unsigned char* col_arr;
    unsigned RGBval;
    
    //get offset coord
    int i_off = (blocks_offset)/(width - 2);
    int j_off = (blocks_offset)%(width - 2);
    blocks_offset = ((width)*(i_off)+(j_off));
    //get iter. coord
    int i = (blocks_offset)/(width);
    int j = (blocks_offset)%(width);
    
    while (thread_blks > 0)
    {
        if (j == width-2)
        {
            j = 0;
            i++;
        }
        
        for (int color_val = 0; color_val < 4; color_val++)
        {
            result_conv = 255;
            
            if (color_val != 3)
            {
                result_conv = 0;
                
                for (int ii = 0; ii < 3; ii++)
                {
                    for (int jj = 0; jj < 3; jj++)
                    {
                        int new_i = i + ii;
                        int new_j = j + jj;
                        unsigned offs = ((width)*(new_i)+(new_j));
                        col_arr = (input_image + (offs*4));
                        RGBval = col_arr[color_val];
                        result_conv = result_conv + RGBval * w[ii][jj];
                    }
                }
                
                // Normalize as required
                if (result_conv < 0)
                {
                    result_conv = 0;
                }
                else if(result_conv > 255)
                {
                    result_conv = 255;
                }
            }
            
            unsigned offs = ((width-2)*(i)+(j));
            col_arr = (output_image + (offs*4));
            col_arr[color_val] = result_conv;
        }
        j++;
        thread_blks--;
    }
    pthread_exit(NULL);
}

// Area for output image (in px amount)
unsigned calculateTotalOutputLength(original_w, original_h)
{
    unsigned result = 0;
    int offset = 2; //due to convolution
    result = (original_w - offset) * (original_h - offset);
    return result;
}

// Get blocks # for each thread to process
unsigned sliceBlocks(unsigned output_length, int number_of_threads)
{
    unsigned blocks_for_each_thread = 0;
    blocks_for_each_thread = output_length / number_of_threads;
    blocks_for_each_thread = (blocks_for_each_thread == 0) ? 1 : blocks_for_each_thread;
    return blocks_for_each_thread;
}


// Main handler. Sets up the necessary paraemters for thread arguments struct, including the division of work for each thread
// and issues n amount of threads. Calls lodepng necessary functions for image load/write.
int convolve(char* input_filename, char* output_filename, int number_of_threads)
{
    // vars for convolution
    unsigned char *input_image, *output_image;
    unsigned width;
    unsigned height;
    unsigned output_img_len;
    unsigned thread_blks;
    
    pthread_t thread_ids[number_of_threads];
    thread_arg_t thread_args[number_of_threads];
    
    int error1 = lodepng_decode32_file(&input_image, &width, &height, input_filename);
    if(error1) {
        printf("Error %u: %s\n", error1, lodepng_error_text(error1));
        return -1;
    }

    output_img_len = calculateTotalOutputLength(width, height);
    output_image = (unsigned char*) malloc(4 * output_img_len);
    thread_blks = sliceBlocks(output_img_len, number_of_threads);
    
    unsigned extra_blks = output_img_len - (number_of_threads * thread_blks);
    
    for (int i = 0; i < number_of_threads; i++) {
        thread_args[i].input_image = input_image;
        thread_args[i].output_image = output_image;
        thread_args[i].image_width = width;
        thread_args[i].image_height = height;
        thread_args[i].id = i;
        thread_args[i].blks = thread_blks;
        thread_args[i].blks_off = thread_blks * i;
        
        //due to the image not being sliced in perfect amount of blocks, assign the rest of blocks a
        //extra work for last thread, adjust offset in others
        if(i == number_of_threads - 1 && extra_blks > 0)
        {
            thread_args[i].blks += extra_blks;
            for (int j = 0; i < number_of_threads - 1; j++)
            {
                thread_args[j].blks_off += extra_blks;
            }
        }
    }
    
    //Start time at thread exec start
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    printf("Joining threads! Waiting.. \n");
    for (int i = 0; i < number_of_threads && i < output_img_len; i++)
    {
        pthread_create(&thread_ids[i], NULL, do_convolution_process, (void *)&thread_args[i]);
    }

    for (int i = 0; i < number_of_threads; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }
    
    //End time
    gettimeofday(&end, NULL);
    long interval = ((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec));
    float converted_interval = (float)interval/1000.0;
    printf("\n\nImage Processing took time : %.2f ms\n", converted_interval);
    
    printf("Writing to file... \n");
    lodepng_encode32_file(output_filename, output_image, width - 2, height - 2);
    printf("Writing to file...DONE \n");
    
    printf("Freeing Ressources..\n");
    free(output_image);
    free(input_image);
    printf("Freeing Ressources..DONE\n");
    return 0;
}

//Main method
int main(int argc, char *argv[])
{
    
    //Usage
    if(argc<4)
    {
        printf("INVALID NUMBER OF ARGUMENTS Usage: ./convolve <input PNG> <output PNG> <# of threads>\n");
        return -1;
    }
    
    int return_code = 0;
    char* input_filename = argv[1];
    char* output_filename = argv[2];
    int number_of_threads = atoi(argv[3]);
    
    if(number_of_threads < 1)
    {
        printf("ERROR: Can't have less than 1 thread.\n");
        return -1;
    }
    
    // Call the main handler function
    return_code = convolve(input_filename, output_filename, number_of_threads);
    
    if (return_code !=0)
    {
        return return_code;
    }
    else
    {
        return 0;
    }
}