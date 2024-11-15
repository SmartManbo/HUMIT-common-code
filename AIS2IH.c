#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <pthread.h>

/*
The purpose of this program is multi-channel I2C data acquisition.
The number of sensors and the number of samples are specified via command-line arguments.
First, pass the number of sensors, then pass the number of samples.
The number of sensors must be specified and must be between 1 and 4.
The number of samples is optional. By default, data is collected for 10 seconds,
with 1600 samples per second. The generated data is stored in the ./acc_data directory.
*/

#define DEBUG_MOD 0 // Debug mode switch, detailed information will be printed in debug mode

#define WHO_AM_I 0x0F
#define CTRL1 0x20
#define CTRL2 0x21
#define FIFO_CTRL 0x2E
#define CTRL6 0x25
#define STATUS 0x27 // Status register, the least significant bit indicates new data is available when it is 1
#define OUT_X_L 0x28
#define OUT_X_H 0x29
#define OUT_Y_L 0x2A
#define OUT_Y_H 0x2B
#define OUT_Z_L 0x2C
#define OUT_Z_H 0x2D
#define SENSOR_ADDRESS 0x19   // Sensor address
#define BUFFER_SIZE 6         // Buffer array size
#define SAMPLE_FREQUENCY 1600 // Sampling frequency
#define DEFAULT_TIME 10       // Default sampling time

int sampleNum = SAMPLE_FREQUENCY * DEFAULT_TIME; // Total number of samples
int sensorNum = 0;                               // Number of sensors passed via command-line
const char data_path[] = "acc_data";             // Data storage directory

// Define a structure to hold the parameters of each accelerometer
typedef struct SensorInfo
{
    int sensorIndex;             // Sensor index
    int i2cFile;                 // Corresponding I2C device
    char msgBuffer[BUFFER_SIZE]; // Buffer array
} SensorInfo, *pSensor;

// Function prototypes

void prepare_args(int argc, char *argv[]);                                     // Check command-line arguments and prepare
void initSensors(pSensor sensorPointer);                                       // Initialize the basic information of each sensor
int writeRegister(int i2cFile, unsigned char regAddress, unsigned char value); // Write data to a specific register of an I2C device
unsigned char readRegOneByte(int i2cFile, unsigned char regAddress);           // Read 1 byte from a specific register of an I2C device
void readRegBytes(pSensor arg, unsigned char regAddress, int bufferSize);      // Read `bufferSize` bytes from a specific register of an I2C device
int setup(pSensor arg);                                                        // Initialize and configure an I2C device
void loop(pSensor arg);                                                        // Loop to read data from an I2C device and write it to a file
void *sensorThread(void *arg);                                                 // Thread executed by each accelerometer

int main(int argc, char *argv[])
{
    // Check command-line arguments and do some preparing work
    prepare_args(argc, argv);
    // Define an array of structures to store the information of each accelerometer
    SensorInfo accArgs[sensorNum];
    // Initialize the basic information of each sensor
    initSensors(accArgs);
    // Create a thread for each accelerometer
    pthread_t threads[sensorNum];
    for (int i = 0; i < sensorNum; ++i)
    {
        // Only create a thread if the sensor opened successfully
        if (accArgs[i].i2cFile != -1)
        {
            // Create a new thread that will execute the sensorThread function, and pass the basic information of the accelerometer via accArgs
            // If the thread is created successfully, pthread_create returns 0; otherwise, it returns a non-zero value
            if (pthread_create(&threads[i], NULL, sensorThread, (void *)&accArgs[i]) != 0)
            {
                printf("Failed to create thread %d\n", i);
                exit(EXIT_FAILURE);
            }
        }
    }
    // The main thread waits for all threads to finish
    for (int i = 0; i < sensorNum; ++i)
    {
        pthread_join(threads[i], NULL);
    }
    printf("All data was saved at '%s' \n", data_path);
    return 0;
}

// Function: Initialize the basic information of each sensor
void prepare_args(int argc, char *argv[])
{
    // Check if at least one command-line argument is passed
    if (argc < 2)
    {
        printf("Error! You must assign sensor number!\n");
        exit(EXIT_FAILURE);
    }
    // Receive the sensorNum passed via command-line
    sensorNum = atoi(argv[1]);
    if (sensorNum < 1 || sensorNum > 4)
    {
        printf("Error! Sensor number must be between 1 and 4!\n");
        exit(EXIT_FAILURE);
    }
    // Check and handle the optional parameter sampleNum
    if (argc > 2)
    {
        // Receive the number of samples passed via command-line
        sampleNum = atoi(argv[2]);
        // It must not be less than the minimum number of samples
        if (sampleNum < SAMPLE_FREQUENCY)
            sampleNum = SAMPLE_FREQUENCY;
    }
    struct stat st;
    // Check if the file storage directory exists, if not, create it
    if (!(stat(data_path, &st) == 0 && S_ISDIR(st.st_mode)))
    {
        if (mkdir(data_path, 0755) != 0)
        {
            perror("Failed to create saving directory");
            return EXIT_FAILURE;
        }
    }
    printf("Each sensor will collect %d samples in %.2lf seconds.\n", sampleNum, (double)sampleNum / SAMPLE_FREQUENCY);
}

// Function: Initialize the basic information of each sensor
void initSensors(pSensor sensorPointer)
{
    for (int i = 0; i < sensorNum; i++)
    {
        // Set the index for each sensor
        (sensorPointer + i)->sensorIndex = i;
        // Check if each I2C device is successfully opened
        char i2cPattern[16];
        snprintf(i2cPattern, sizeof(i2cPattern), "/dev/i2c-%d", i);
        (sensorPointer + i)->i2cFile = open(i2cPattern, O_RDWR);
        if ((sensorPointer + i)->i2cFile == -1)
        {
            printf("Failed to open /dev/i2c-%d\n", i);
        }
    }
}

// Function: Write data to a specific register of an I2C device
int writeRegister(int i2cFile, unsigned char regAddress, unsigned char value)
{
    // Pack the register address and data into a 2-byte array `buf`
    unsigned char buf[2] = {regAddress, value};
    if (DEBUG_MOD)
    {
        printf("Try to write 0x%02x into register 0x%02x\n", value, regAddress);
    }
    // Try to write the two bytes in `buf` to the I2C device file, and check if the number of bytes written equals sizeof(buf)
    // If not, it indicates a failure, and an error message is printed before exiting the program
    if (write(i2cFile, buf, sizeof(buf)) != sizeof(buf))
    {
        perror("Failed to write to I2C device");
        return 1;
    }
    if (DEBUG_MOD)
    {
        printf("Write successfully!\n");
    }
    return 0;
}

// Function: Read 1 byte from a specific register of an I2C device
unsigned char readRegOneByte(int i2cFile, unsigned char regAddress)
{
    if (DEBUG_MOD)
        printf("Try to read 1 byte from register 0x%02x.\n", regAddress);
    // Try to write the register address `regAddress` to the I2C device file, and check if the number of bytes written equals sizeof(regAddress)
    // If not, it indicates a failure, and an error message is printed before exiting the program
    if (write(i2cFile, &regAddress, sizeof(regAddress)) != sizeof(regAddress))
    {
        perror("Failed to write to I2C device");
        exit(EXIT_FAILURE);
    }
    unsigned char value;
    // Try to read 1 byte from the I2C device file into the variable `value`, and check if the number of bytes read equals 1
    // If not, it indicates a failure, and an error message is printed before exiting the program
    if (read(i2cFile, &value, sizeof(value)) != sizeof(value))
    {
        perror("Failed to read from I2C device");
        exit(EXIT_FAILURE);
    }
    if (DEBUG_MOD)
        printf("Read successfully!\n");
    return value;
}

// Function: Read `bufferSize` bytes from a specific register of an I2C device
void readRegBytes(pSensor arg, unsigned char regAddress, int bufferSize)
{
    if (DEBUG_MOD)
        printf("Try to read %d bytes from register 0x%02x\n", bufferSize, regAddress);
    if (write(arg->i2cFile, &regAddress, sizeof(regAddress)) != sizeof(regAddress))
    {
        perror("Failed to write to I2C device");
        exit(EXIT_FAILURE);
    }
    // Try to read `bufferSize` bytes from the I2C device file into the buffer array `msgBuffer`, and check if the number of bytes read equals `bufferSize`
    // If not, it indicates a failure, and an error message is printed before exiting the program
    if (bufferSize > 0)
    {
        if (read(arg->i2cFile, arg->msgBuffer, bufferSize) != bufferSize)
        {
            perror("Failed to read from I2C device");
            exit(EXIT_FAILURE);
        }
        if (DEBUG_MOD)
            printf("Read successfully!\n");
    }
    else
    {
        printf("Failed to read %d bytes!\n", bufferSize);
    }
}

// Function: Initialize and configure an I2C device
int setup(pSensor arg)
{
    // Use the ioctl function to set the slave address in the I2C communication. If it fails, print an error message and exit the program.
    if (ioctl(arg->i2cFile, I2C_SLAVE, SENSOR_ADDRESS) < 0)
    {
        perror("Failed to acquire bus access and/or talk to slave");
        return 1;
    }

    // Configure the accelerometer
    int ret = 0;
    ret = writeRegister(arg->i2cFile, CTRL1, 0x97); // CTRL1 - 1600 Hz output data rate, high-performance mode
    if (ret != 0)
        return 1;
    ret = writeRegister(arg->i2cFile, CTRL2, 0x04); // CTRL2 - IF_ADD_INC: Automatically increment register address during multi-byte access
    if (ret != 0)
        return 1;
    ret = writeRegister(arg->i2cFile, FIFO_CTRL, 0xD0); // FIFO_CTRL [FMode2 FMode1 FMode0 FTH4 FTH3 FTH2 FTH1 FTH0], Continuous mode: New samples overwrite old ones when FIFO is full
    if (ret != 0)
        return 1;
    ret = writeRegister(arg->i2cFile, CTRL6, 0x30); // CTRL6 - Full-scale selection: ±16 g
    if (ret != 0)
        return 1;

    // Check the configuration
    if (DEBUG_MOD)
    {
        char msg[64];
        sprintf(msg, "WHO_AM_I: 0x%02x", readRegOneByte(arg->i2cFile, WHO_AM_I));
        puts(msg);
        sprintf(msg, "CTRL1: 0x%02x", readRegOneByte(arg->i2cFile, CTRL1));
        puts(msg);
        sprintf(msg, "CTRL2: 0x%02x", readRegOneByte(arg->i2cFile, CTRL2));
        puts(msg);
        sprintf(msg, "FIFO_CTRL: 0x%02x", readRegOneByte(arg->i2cFile, FIFO_CTRL));
        puts(msg);
        sprintf(msg, "CTRL6: 0x%02x", readRegOneByte(arg->i2cFile, CTRL6));
        puts(msg);
    }
    return 0;
}

// Function: Loop to read data from an I2C device and write it to a file
void loop(pSensor arg)
{
    // Get the current time
    time_t currentTime;
    struct tm *localTime;
    time(&currentTime);
    localTime = localtime(&currentTime);
    // Format the time
    char formattedTime[16];
    strftime(formattedTime, sizeof(formattedTime), "%Y%m%d_%H%M%S", localTime);
    // Use the formatted time as part of the filename
    char outputFileName[64];
    snprintf(outputFileName, sizeof(outputFileName), "%s/%s_sensor%d.csv", data_path, formattedTime, arg->sensorIndex);
    // Open the file for writing
    FILE *file = fopen(outputFileName, "a");
    if (file == NULL)
    {
        perror("Failed to open output file for writing");
        exit(EXIT_FAILURE);
    }
    // Total number of bytes to read, since each complete data sample contains six bytes, i.e., [X_L, X_H, Y_L, Y_H, Z_L, Z_H]
    int totalByteNum = sampleNum * 6;
    // Continue reading as long as there are unread bytes
    while (totalByteNum > 0)
    {
        // Check the status register to determine if new data is available
        if (readRegOneByte(arg->i2cFile, STATUS) & 1 == 1)
        {
            // Read data into the buffer
            readRegBytes(arg, OUT_X_L, BUFFER_SIZE);
            // Combine the high and low bytes
            short OUT_X = (short)(arg->msgBuffer[1] << 8 | arg->msgBuffer[0]);
            short OUT_Y = (short)(arg->msgBuffer[3] << 8 | arg->msgBuffer[2]);
            short OUT_Z = (short)(arg->msgBuffer[5] << 8 | arg->msgBuffer[4]);
            // Right shift by two bits
            int dataX = OUT_X >> 2;
            int dataY = OUT_Y >> 2;
            int dataZ = OUT_Z >> 2;
            // Write to the file
            fprintf(file, "%d,%d,%d\n", dataX, dataY, dataZ);
            // Update the remaining byte count
            totalByteNum -= BUFFER_SIZE;
        }
    }
    fclose(file); // Close the output file
    printf("\nSensor %d completed!\n", arg->sensorIndex);
}

// Thread executed by each accelerometer
void *sensorThread(void *arg)
{
    // Convert the generic pointer to an accelerometer structure pointer
    pSensor info = (pSensor)arg;
    // Check if the sensor was initialized correctly before proceeding
    if (info->i2cFile == -1)
    {
        printf("Sensor %d initialization failed. Exiting thread.\n", info->sensorIndex);
        pthread_exit(NULL);
    }
    // Configure the accelerometer parameters
    int ret = setup(info);
    if (ret != 0)
    {
        printf("Sensor %d setup failed. Exiting the thread.\n", info->sensorIndex);
        pthread_exit(NULL);
    }
    // Loop to read data
    loop(info);
    // Close the I2C device
    close(info->i2cFile);
    // Exit the thread
    pthread_exit(NULL);
}
