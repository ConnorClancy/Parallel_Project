#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
int sum(int inputArr[], size_t size){
        int total = 0;
        for(int i = 0; i < size; i++){
                total = total + inputArr[i];
        }
        return total;
}

int mode(int inputArr[], size_t size){
        int counter[] = {0,0,0,0,0,0,0,0,0,0,0};
        for(int i = 0; i < size; i++){
                counter[inputArr[i]]++;
        }
        int max = 0;
        int index = 0;
        size_t counterSize = sizeof(counter)/4;
        for(int i = 0; i < counterSize; i++){
                if(counter[i] > max){
                        max = counter[i];
                        index = i;
                }
        }
        return index;
}

int mode_freq(int inputArr[], size_t size, int mode){
        int freq = 0;
        for(int i = 0; i < size; i++){
                if(inputArr[i] == mode){
                        freq++;
                }
        }
        return freq;
}

int dot_prod(int inputA[], int inputB[], size_t size){
        int total = 0;
        for(int i = 0; i < size; i++){
                total = total + (inputA[i] * inputB[i]);
        }
        return total;
}

int* getModeCount(int inputArr[], size_t size){
        static int counter[] = {0,0,0,0,0,0,0,0,0,0,0};
        for(int i = 0; i < size; i++){
                counter[inputArr[i]]++;
        }
        return counter;
}

int main(int argc, char** argv) {
        // Initialize the MPI environment
        MPI_Init(NULL, NULL);

        // Get the number of processes
        int world_size;
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        if(world_size == 1 || world_size > 3) {
                printf("incorrect number of processors");
                MPI_Abort(MPI_COMM_WORLD, -1);
                return 0;
        }
        
        int SIZE=10000;

        // Get the rank of the process
        int world_rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

        //Declare global variables used for collective COMMs
        int PARTITIONS[world_size];
        int test[10000];  //(int*)malloc(sizeof(int) * 10000);
        if(world_rank == 0){
                FILE *fp = fopen("/tmp/assign1/Array.csv", "r");
                if(fp == NULL){
                        printf("file not found");
                        MPI_Abort(MPI_COMM_WORLD, -1);
                        return 0;
                }
                int c = 0;
                int i = 0;
                int sep = 0;
                // Take in array from .csv
                while(fscanf(fp, "%d,", &test[i]) && i < SIZE){
                        i++;
                }
                i = 0;

                int m = 0;
                m =  mode(test, SIZE);
                printf("root process only:\n");
                printf("\nsum: %d", sum(test, SIZE));
                printf("\nmode: %d", m);
                printf("\nmode frequency: %d", mode_freq(test, SIZE, m));

                int *halfway;
                halfway = test + (SIZE/2);
                printf("\ndot product: %d\n\n", dot_prod(test, halfway, SIZE/2));                

                fclose(fp);
                /*
                Prep phase for parallel implementation
                        - if SIZE/np is even then the workload for each process is set as the even divisions
                        - else the modulo is taken, and the remainder is divided evenly amoung np processes. The each number is reduced until all 
                          partitions and the number of leftover inputs are even, these leftovers are then round robined out in sets of 2 until
                          non are left. E.G: 10000 with np 3 = 3334, 3334 and 3332 numbers to each processor respectively. 
                */
                int leftovers = 0;
                int num_part = world_size;
                if(SIZE % world_size ==0){
                        int buf = SIZE/world_size;
                        int i = 0;
                        while(i < world_size){
                                PARTITIONS[i] = buf;
                                i = i + 1;
                        }
                }
                else{
                        int currSize = 0;
                        int leftovers = 0;
                        int buf = 0;
                        currSize = SIZE;
                        while(currSize % world_size != 0){
                                currSize--;
                                leftovers++;
                        }
                        buf = currSize / world_size;
                        for(int i = 0; i < world_size; i++){
                                PARTITIONS[i] = buf;
                        }
                        i = 0;
                        while(leftovers != 0){
                                if(PARTITIONS[i] % 2 != 0){
                                        PARTITIONS[i]--;
                                        leftovers++;
                                }
                                else if(leftovers % 2 == 0){
                                        PARTITIONS[i]+=2;
                                        leftovers-=2;
                                }
                                i = (i + 1)%world_size;
                        }
                        
                }       
        }
        
        //Parallel part - using MPI
        MPI_Status stat;
        int partition_size;
        int offset[world_size];
        offset[0] = 0;
        //only the root process goes here
        if(world_rank == 0){
                partition_size = PARTITIONS[0];
                //send size of segment related to each process to that process
                for(int i = 1; i < world_size; i++){
                        int * p = &PARTITIONS[i];
                        //send partition size p from process 0 toprocess i
                        MPI_Send(p, 1, MPI_INT, i, i, MPI_COMM_WORLD);
                }
                for(int i = 1; i < world_size; i++){
                        offset[i] = offset[i-1] + PARTITIONS[i-1];
                }
        }
        else{
                //receive segment size from root in every other process
                MPI_Recv(&partition_size, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &stat);
        }

        //Recbuff is declared of size sent to process by root previously.
        int recbuf[partition_size];
        //Send segments of 'test' out to each process from 0, each one of size PARTITIONS[world_rank] starting at the address test[offset[world_rank]
        MPI_Scatterv(test, PARTITIONS, offset, MPI_INT, recbuf, PARTITIONS[world_rank], MPI_INT, 0, MPI_COMM_WORLD);

        int SUM_MODE[] = {0,0,0,0,0,0,0,0,0,0,0,0};
        int results[12];
        results[0] = sum(recbuf, partition_size);

        //Each process gets frequency of all numbers in its partition
        int *rPtr;
        rPtr = getModeCount(recbuf, partition_size);
        for(int i = 1;i < 12; i++){
                results[i] = *(rPtr + (i-1));
        }

        //All processes (including root) send their results back to the root process, MPI_SUM sums the arrays while doing this action
        MPI_Reduce(results, SUM_MODE, 12, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
        int *halfPartition = (int*)malloc(sizeof(world_size));
        int *aOffset = (int*)malloc(sizeof(world_size));
        int *bOffset = (int*)malloc(sizeof(world_size));
        if(world_rank ==0){
                int max = -1;
                int index = 0;
                //find mode
                for(int i = 0; i < 11; i++){
                        if(SUM_MODE[i+1] > max){
                                max = SUM_MODE[i+1];
                                index = i;
                        }
                }
                printf("parallel bit: \nsum: %d\nmode: %d\nmode frequency: %d\n",SUM_MODE[0], index, max);
                aOffset[0] = 0;
                bOffset[0] = sizeof(test)/8;
                for(int i = 0; i < world_size;i++){
                        halfPartition[i] = PARTITIONS[i]/2;
                        if(i >= 1){
                                aOffset[i] = aOffset[i-1] + halfPartition[i-1];
                                bOffset[i] = bOffset[i-1] + halfPartition[i-1];
                        }
                }
        }
        //Send value of haldPartition to all other processes so that they know how large of an input to take
        MPI_Bcast(halfPartition, world_size, MPI_INT, 0, MPI_COMM_WORLD);
        int p = halfPartition[world_rank];
        int *arec = (int*)malloc(sizeof(int) * p);
        int *brec = (int*)malloc(sizeof(int) * p);
        int *pointer;
        pointer=test+(SIZE/2);
        //Send first and second half of input array split into np partitions each, used for dot prod
        MPI_Scatterv(test, halfPartition, aOffset, MPI_INT, arec, p, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Scatterv(test, halfPartition, bOffset, MPI_INT, brec, p, MPI_INT, 0, MPI_COMM_WORLD);

        int dot = dot_prod(arec, brec, p);
        int dotReturn=0;
        MPI_Reduce(&dot,&dotReturn, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
        if(world_rank == 0){
                printf("\nDot Product: %d\n", dotReturn);
        }
        //close MPI and clean up running processes.
        MPI_Finalize();
}

        