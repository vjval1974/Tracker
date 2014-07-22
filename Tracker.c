//-------------------------------------------------------------------------
// Author: Brad Goold
// Date: 19 Jul 2014
// Email Address: W0085400@umail.usq.edu.au
// 
// Purpose:
// Pre:
// Post:
//-------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <mysql.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "sfsource.h"
#include "serialsource.h"

static char *msgs[] = {
  "unknown_packet_type",
  "ack_timeout"	,
  "sync"	,
  "too_long"	,
  "too_short"	,
  "bad_sync"	,
  "bad_crc"	,
  "closed"	,
  "no_memory"	,
  "unix_error"
};






#define RANGE 48 // range of RSSI values
#define PACKET_SIZE 21 // Size of the packet received from the forwarder

//DEFAULT VALUES
#define X_SIZE 5 // physical size in the x direction
#define Y_SIZE 5 // physical size in the y direction
#define NUM_SAMPLES 10 //number of samples for averaging table
#define NUM_SENSORS 3 //number of absolute position sensors
#define MAX_SENSORS 20
#define MAX_SAMPLES 100

// Byte locations in packet. 
#define SOURCE_ID 13
#define SEQ_NO 17
#define RSSI 16
#define READING 19
#define BUFFER_SIZE 50

// Data structure to hold packet information.
struct Packet { 
    uint8_t source_id; // sensor that made the packet
    uint16_t seq_no;  // sent from target to synchronise packets
    uint8_t rssi;    // Received Signal Strength value
    uint16_t reading;  // reading from sensor (temp)
    uint8_t lqi; // link quility indicator value. 
} Rec_Packet, // Received Packet.  
    Packet_Buffer[BUFFER_SIZE]; // Packet History 

// Settings struct
struct settings {
    unsigned char x_size; // measurement units in x direction
    unsigned char y_size; // measurement units in y direction
    unsigned char num_samples; // Number of samples to take
    unsigned char num_sensors; // Number of static sensors
    uint8_t ids[MAX_SENSORS]; // ID's of the static sensors
} g_settings;


// Database information
const char *server = "localhost";

const char *user = "root";
const char *password = "braD1978"; /* set me first */
const char *database = "Tracker";


//=============================================================
// Function Prototypes
//=============================================================

// Sensor Network Access
//void profiling(char * host, char * port); 
void profiling(const char * dev, const int baud, void (*message)(serial_source_msg problem));
unsigned char get_sensor_id(const char * dev, const int baud, void (*message)(serial_source_msg problem));
//void Monitoring(char * host, char * port);
void Monitoring(const char *dev, const int baud, void (*message)(serial_source_msg problem));
// Database Access
unsigned char check_db_health(void);
unsigned char set_g_settings(void);
unsigned char store_g_settings(void);
unsigned char store(struct Packet P, uint8_t x_pos, uint8_t y_pos);
unsigned char create_tables(void);
unsigned char clear_database(void);
unsigned char populate_main(void);
unsigned char store_monitor(struct Packet P, int sample);
unsigned char get_position(void);

//User interface
void clearScreen(); 
void display_settings(void); 
void stderr_msg(serial_source_msg problem)
{
  fprintf(stderr, "Note: %s\n", msgs[problem]);
}


//=============================================================


int main(int argc, char **argv)
{
    int userInput=0;
    const char * menuLine1 = "1. Profile";
    const char * menuLine2 = "2. Monitoring";
    const char * menuLine3 = "3. Set Boundaries";
    const char * menuLine4 = "4. Setup";
    const char * menuLine5 = "5. RESET TO DEFAULT SETTINGS";
    const char * menuPrompt = "Please choose an item: ";
    const char * menuLine9 = "9. EXIT";
    char * dev;
    int baud;
 
    int i;
    for (i = 0; i < MAX_SENSORS;i++)
        g_settings.ids[i] = 0;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <device> <rate> - dump packets from a serial port\n", argv[0]);
        exit(2);
    }
      
    dev = argv[1];
    baud = atoi(argv[2]);

    
 
    if(check_db_health() == EXIT_FAILURE)
    {
        printf("Database is corrupt, must recreate...\n");
        clear_database(); //drop the database and recreate it
        set_g_settings(); //user enters values
        get_sensor_id(dev, baud, stderr_msg); // search network for sensor id's
        
        create_tables(); // create tables based on received values
        store_g_settings(); //store the settings in the g_settings
                            //table     
    }

    while (userInput != 9)
    {
        // Print Menu
       
        clearScreen();
        //display_settings();
       
        printf("%s\n", menuLine1);
        printf("%s\n", menuLine2);
        printf("%s\n", menuLine3);
        printf("%s\n", menuLine4);
        printf("%s\n", menuLine5);
        printf("%s\n", menuLine9);
        printf("%s\n", menuPrompt);
   
        // Get user input 
        scanf("%i", &userInput);
        // Clear the standard input stream
        scanf("%*[^\n]");
        scanf("%*c");

        clearScreen();       
        //printf("user input was %d\n", userInput);    
        switch (userInput) 
        {
        case 1: 
        {

            fflush(stdout);
            profiling(dev, baud, stderr_msg);
            populate_main();
            break; 
        }
        case 2: 
        {
            Monitoring(dev, baud, stderr_msg);
            get_position();
            break; 
        }
        case 3: 
        {
            
            break; 
        }
        case 4: 
        {
            clear_database(); //drop the database and recreate it
            set_g_settings(); //user enters values
            get_sensor_id(dev, baud, stderr_msg); // search network for sensor id's
            //display_settings();
            create_tables(); // create tables based on received values
            store_g_settings(); //store the settings in the g_settings
            //table     
            break; 
        }
        case 5: 
        {
            break; 
        }

        case 9: //EXIT
        {
            printf("Ok Bye!\n\n");
            exit(0);
        }
        default:
        {
            printf("invalid selection");
            break;
        }
        }
        fflush(stdout);

    }
    //---------------------------
    //free(id_array);
    return 0;
}


// HELPER FUNCTIONS
void clearScreen()
{
    const char* CLEAR_SCREE_ANSI = "\e[1;1H\e[2J";
    write(STDOUT_FILENO,CLEAR_SCREE_ANSI,12);
}



void profiling(const char *dev, const int baud, void (*message)(serial_source_msg problem)){
    serial_source fd; //file descriptor
    int ii, kk, ll, x_pos = 1, y_pos = 1; // iterators
    int len; //length of the packet read from the forwarder
    uint8_t ph; //placeholder for triangular swap
    uint16_t num_stored = 0; // Number of complete sequences stored 
    unsigned char rpacket[PACKET_SIZE]; //reverse packet.
    unsigned char found_items[g_settings.num_sensors];
    uint16_t search_variable, item; 
    unsigned char num_found = 0; 
    unsigned char * packet; // pointer to the packet read from the
                            // serial forwarder
    char c = 0;
    //--------------------------------------------------
    // Open the serial forwarder and get file descriptor
    //--------------------------------------------------
   
    for (x_pos = 1; x_pos <= g_settings.x_size; x_pos++)
    {
        for (y_pos = 1; y_pos <= g_settings.y_size; y_pos++)
        {
            printf("Please put the sensor at position X:%d, Y:%d\r\n", x_pos, y_pos);
            
            fflush(stdin);
            printf("%s Confirm with ENTER:", &c);
            // Get user input 
            scanf("%*c");
            // Clear the standard input stream
            //scanf("%*[^\n]");
            //scanf("%*c");
            fd = open_serial_source(dev, baud, 0, stderr_msg);
                if (!fd)
                {
                    fprintf(stderr, "Couldn't open serial port at %s:%d\n",
                            dev, baud);
                    exit(1);
                }

            //--------------------------------------------------
            // loop through until we get the number of samples we need
            // for each location
            // --------------------------------------------------------
            while(num_stored < g_settings.num_samples)
            {
                // Read Packet
                //------------
                //packet = read_sf_packet(fd, &len);
                
                packet = read_serial_packet(fd, &len);
                if (!packet)
                    exit(0);
                //make a copy
                memcpy(rpacket, packet, len);
        
                //triangular swap for wrong endianness.
                //------------------------------------
                ph = rpacket[18];
                rpacket[18] = rpacket[17];
                rpacket[17] = ph; 
       
                ph = rpacket[20];
                rpacket[20] = rpacket[19];
                rpacket[19] = ph;
        
                // build Struct from packet read
                // -----------------------------
                Rec_Packet.source_id = rpacket[SOURCE_ID];
                Rec_Packet.rssi = rpacket[RSSI];
                memcpy(&Rec_Packet.seq_no, &rpacket[SEQ_NO], 2);
                memcpy(&Rec_Packet.reading, &rpacket[READING], 2);

                // We are only looking for specific packets
                if (len == PACKET_SIZE)
                {
                    //Validate Addresses (dont need base or target) 
                    if (Rec_Packet.source_id != 0x00 &&
                        Rec_Packet.source_id != 0x08)  
                    {               
                        //shift right buffer. 
                        //printf("shifting right...\n");
                        for (kk = BUFFER_SIZE-1; kk > 0; kk--)
                        {
                            memcpy(&Packet_Buffer[kk], 
                                   &Packet_Buffer[kk-1], sizeof(struct Packet));
                        }
      
                        // put the read value into the first element of the
                        // buffer
                        // memcpy(buffer[0], rpacket, PACKET_SIZE);
                        memcpy(&Packet_Buffer[0], &Rec_Packet, sizeof(struct Packet));

                        /* printf("ID =  %d, RSSI = %d, SEQ = %d, Reading = %d\n ", 
                           Packet_Buffer[0].source_id,
                           Packet_Buffer[0].rssi,
                           Packet_Buffer[0].seq_no,
                           Packet_Buffer[0].reading);
                        */
                        // Find how many instances of each sequence number in
                        // the buffer there are. If there are the name amount
                        // of instances as there are sensors, then we have a
                        // full suite and we can store these instances in the DB
                        search_variable = Packet_Buffer[0].seq_no;
                        //printf("searching for %04u\n", search_variable);
                        for (ll = 0; ll < BUFFER_SIZE; ll++)
                        {
                            item = Packet_Buffer[ll].seq_no;
                           
                            if (item == search_variable)
                            {
                                num_found++;
                                //      printf("found %d %u's\n", 
                                //       num_found, 
                                //       search_variable);
                                
                                found_items[num_found-1] = ll; //points
                                //to element
                                fflush(stdout); 
                                
                                
                                if (num_found == g_settings.num_sensors)
                                {
                                    // we have a full set, 
                                    num_stored++;

                                    for (ii = 0; ii < g_settings.num_sensors; ii++)
                                        store(Packet_Buffer[found_items[ii]], x_pos, y_pos); 
                                    
                                }// if (num_found... 
                            }// if item == search variable...
                        }//for ll = 0 ...
                        num_found = 0;
                    } //validate addresses
                }// if len == packet_size

                free((void *)packet);
            }//while num_stored
            num_stored = 0; // reset for next loop
        }//for y_pos
    }//for x_pos

}


unsigned char store(struct Packet P, uint8_t x_pos, uint8_t y_pos){
    MYSQL *conn;
    char table_name[30]; 
    char query[512];

    sprintf(table_name, "s%02d_pos%02d_%02d", P.source_id, x_pos, y_pos);
    
    conn = mysql_init(NULL);
    //Connect to the Database
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }

    sprintf(query, "insert into %s (rssi) VALUES  (%d);", table_name, P.rssi);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }
    //printf("Query sent: %s\n", query);
    mysql_close(conn);
    return 0;
}

unsigned char store_g_settings(void)
{
    MYSQL *conn;
    char query[512];
    conn = mysql_init(NULL);
    int ii;

    //Connect to the Database
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        return 1;
    }
   
   
    // clear current values
    sprintf(query, "delete from g_settings where item = 0");
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }
    
    //insert values from g_settings struct
    sprintf(query, "insert into g_settings (item, x_size, y_size, num_samples, num_sensors) VALUES  (0,%d,%d,%d,%d);", g_settings.x_size, g_settings.y_size, g_settings.num_samples, g_settings.num_sensors);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }

    //clears all rows in the 'sensors' table;
    sprintf(query, "delete from sensors;");
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }

    ii = 0;
    while(g_settings.ids[ii] != 0)
    {
        sprintf(query, "insert into sensors (item,id_number) VALUES  (%d,%d);",ii+1, g_settings.ids[ii]);
        if (mysql_query(conn, query)) 
        {
            fprintf(stderr, "%s\n", mysql_error(conn));
            mysql_close(conn);        
            return 1;
        }   
        ii++;
        
    }
    

    mysql_close(conn);
    return 0;

}


unsigned char create_tables(void)
{
    
    MYSQL *conn;
    char query[256];
    int kk, ii, jj, ll;
    char middle[512], * middle1, * middle2;
    char temp[100], temp1[100], temp2[512];
    
    middle1 = (char *)calloc(512, 1);
    middle2 = (char *)calloc(512, 1);
    
    // initialise 
    for (ii = 0; ii < 512;ii++)
        middle[ii] = '\0';
    

    conn = mysql_init(NULL);
    /* Connect to database */
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        return 1;
    }
    


    sprintf(query, "CREATE TABLE IF NOT EXISTS g_settings(item INT(4) UNIQUE PRIMARY KEY , num_sensors INT(4), x_size INT(4), y_size INT(4), num_samples INT(4)) ENGINE = INNODB;");
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    
    sprintf(query, "CREATE TABLE IF NOT EXISTS sensors(item INT(4) UNIQUE PRIMARY KEY, id_number INT(4)) ENGINE = INNODB;");
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }

   

    
    // initialise the grid so we can use the position values.
    for (kk = 0; kk < g_settings.num_sensors; kk++)
    {
        for (ii = 0; ii < g_settings.x_size; ii++)
        {
            for (jj = 0; jj < g_settings.y_size; jj++)
            {
               
                sprintf(query, "CREATE TABLE IF NOT EXISTS s%02d_pos%02d_%02d(sample INT(4) AUTO_INCREMENT PRIMARY KEY, rssi INT(4)) ENGINE = INNODB;", g_settings.ids[kk], ii+1, jj+1);
                if (mysql_query(conn, query)) {
                    fprintf(stderr, "%s\n", mysql_error(conn));
                    mysql_close(conn);
                    return 1;
                }
            }

        }
    }
    
       
    const char  prefix[] = "CREATE TABLE IF NOT EXISTS Main(position INT(4) UNIQUE PRIMARY KEY ";
    const char  prefix2[] = "CREATE TABLE IF NOT EXISTS Current_reading(position INT(4) UNIQUE PRIMARY KEY ";
    const char  suffix[] =  ") ENGINE = INNODB;";
    const char suffix2[] =  ") ENGINE = INNODB;";
    strcat(middle, prefix);
    strcat(middle2, prefix2);
    for (ll = 0; ll < g_settings.num_sensors; ll++)
    {
        sprintf(temp, ",S%02d_rssi INT(4), S%02d_lqi INT(4)", g_settings.ids[ll], g_settings.ids[ll]);
        sprintf(temp2, ",S%02d_rssi INT(4), S%02d_lqi INT(4)", g_settings.ids[ll], g_settings.ids[ll]);
        strcat(middle, temp);
        strcat(middle2,temp2);
    }
    strcat(middle, suffix);
    strcat(middle2, suffix2);

    if (mysql_query(conn, middle)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
     if (mysql_query(conn, middle2)) 
     {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
     }


    char  pre[] = "CREATE TABLE IF NOT EXISTS monitor(sample INT(4) UNIQUE PRIMARY KEY \0";
    char  suf[] =  ") ENGINE = INNODB;\0";
    strcat(middle1, pre);
    for (ll = 0; ll < g_settings.num_sensors; ll++)
    {
        sprintf(temp1, ",S%02d INT(4)", g_settings.ids[ll]);
        strcat(middle1, temp1);
        
    }
    strcat(middle1, suf);
    printf("%s\n", middle1);
    if (mysql_query(conn, middle1)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    
    for (ii = 0; ii < g_settings.num_samples; ii++)
    {
        sprintf(query, "insert into monitor(sample) VALUES  (%02d);",ii+1);
        if (mysql_query(conn, query)) {
            fprintf(stderr, "%s\n", mysql_error(conn));
            mysql_close(conn);
            return 1;
        }
        
    }
    
    sprintf(query, "CREATE TABLE IF NOT EXISTS diff LIKE Main;");
    if (mysql_query(conn, query)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    
    for (ii = 0; ii < g_settings.x_size; ii++)
    {
        for (jj = 0; jj < g_settings.y_size; jj++)
        {
            sprintf(query, "insert into Main (position) VALUES  (%02d%02d);",ii+1, jj+1);
            if (mysql_query(conn, query)) {
                fprintf(stderr, "%s\n", mysql_error(conn));
                mysql_close(conn);
                return 1;
            }            
sprintf(query, "insert into diff (position) VALUES  (%02d%02d);",ii+1, jj+1);
            if (mysql_query(conn, query)) {
                fprintf(stderr, "%s\n", mysql_error(conn));
                mysql_close(conn);
                return 1;
            }            

        }
    }
   
   
    
    /* close connection */
    
    mysql_close(conn);
    free(middle1);
    free(middle2);
    return 0;
}


unsigned char set_g_settings(void){
    unsigned char *set[4];
    set[0] = &g_settings.num_sensors;
    set[1] = &g_settings.num_samples;
    set[2] = &g_settings.x_size;
    set[3] = &g_settings.y_size;
    int ret = 0;
    unsigned int ans;
    int ii = 0;
    char * questions[4] = {
        "How many sensors are you using?   ",
        "How many samples for profiling?   ",
        "What is the x-size of the area?   ",
        "what is the y-size of the area?   ",
    };
    
    for (ii = 0; ii < 4 ; ii++)
    {
        printf("%s", questions[ii]);
        // Get user input  
        ret = scanf("%i", &ans);
        // Clear the standard input stream
        scanf("%*[^\n]");
        scanf("%*c");
    
        while(ret != 1)
        {
            printf("need a single integer! try again: ");
            // Get user input  
            ret = scanf("%i", &ans);
            // Clear the standard input stream
            scanf("%*[^\n]");
            scanf("%*c");   
        }
        //ans = atoi(&ans);
        printf("ans = %d\n", ans);
        *set[ii] = ans;
    }
    // clearScreen();
    
    return 0;


}

unsigned char clear_database(void)
{
    MYSQL *conn;
    char query[512];
    conn = mysql_init(NULL);
   
    //Connect to the Database
    if (!mysql_real_connect(conn, server,
                            user, password, NULL, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        
        return 1;
    }
   
    // drop the database
    printf("Dropping database\n");
    sprintf(query, "drop database %s", database);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    
    printf("Creating database\n");
    sprintf(query, "create database %s", database);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    sprintf(query, "use %s", database);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
       
        return 1;

    }

    mysql_close(conn);
    return 0;
}





// RSSI Percentage calculation
//-----------------------------
// pct =(double)(( (double)(255-rpacket[16]) / 48.0 ) * 100.0);
// if (pct > 100) 
//    pct = 255;



unsigned char get_sensor_id(const char *dev, const int baud, void (*message)(serial_source_msg problem)){
    serial_source fd; //file descriptor
    int ii, flag = 0, counter = 0;
    int len; //length of the packet read from the forwarder
    uint8_t ph; //placeholder for triangular swap
//    uint8_t * ptr = id_array;
    
    unsigned char rpacket[PACKET_SIZE]; //reverse packet.
    uint8_t found_ids[g_settings.num_sensors];
    
    unsigned char * packet; // pointer to the packet read from the
                            // serial forwarder
    
    int num_ids_found = 0;
    //--------------------------------------------------
    // Open the serial forwarder and get file descriptor
    //--------------------------------------------------
    fd = open_serial_source(dev, baud, 0, stderr_msg);
    if (!fd)
    {
        fprintf(stderr, "Couldn't open serial port at %s:%d\n",
                dev, baud);
        exit(1);
    }

   
    while (num_ids_found < g_settings.num_sensors)
    {            
        // Read Packet
        //------------
        packet = read_serial_packet(fd, &len);
        
        if (!packet)
            exit(0);
        //make a copy
        memcpy(rpacket, packet, len);
        
        //triangular swap for wrong endianness.
        //------------------------------------
        ph = rpacket[18];
        rpacket[18] = rpacket[17];
        rpacket[17] = ph; 
       
        ph = rpacket[20];
        rpacket[20] = rpacket[19];
        rpacket[19] = ph;
        
        // build Struct from packet read
        // -----------------------------
        Rec_Packet.source_id = rpacket[SOURCE_ID];
        Rec_Packet.rssi = rpacket[RSSI];
        memcpy(&Rec_Packet.seq_no, &rpacket[SEQ_NO], 2);
        memcpy(&Rec_Packet.reading, &rpacket[READING], 2);

        // We are only looking for specific packets
        if (len == PACKET_SIZE)
        {
            //Validate Addresses (dont need base or target) 
            if (Rec_Packet.source_id != 0x00 &&
                Rec_Packet.source_id != 0x08)  
            {               
                printf("found id number %d\r\n", Rec_Packet.source_id);
                for (ii = 0; ii < g_settings.num_sensors; ii++)
                {
                    if (Rec_Packet.source_id == found_ids[ii])
                        flag = 1;
                }
                if (flag == 0){
                    found_ids[num_ids_found] = Rec_Packet.source_id;
                    //                  *ptr = found_ids[num_ids_found];
                    g_settings.ids[num_ids_found] = found_ids[num_ids_found];
                
                    // ptr++;
                    num_ids_found++;
                }
                flag = 0;
            }
        }//if len ==
        free((void *)packet);
        counter++;
        if (counter > 100)
        {
            printf("Failed getting sensor id's: too many tries\n");
            return 1;
        }

    }// while
    
    for (ii = 0; ii < g_settings.num_sensors; ii++)
    {
        printf("found id %d = %d\n", ii, found_ids[ii]);
    }
    return 0; 
}

void display_settings(void){
    int ii;
    printf("Contents of the G_SETTINGS struct\n");
    printf("---------------------------------\n");
    printf("Number of Sensors = %d\n", g_settings.num_sensors);
    printf("Number of Samples = %d\n", g_settings.num_samples);
    printf("Size in x direction = %d\n", g_settings.x_size);
    printf("Size in y direction = %d\n", g_settings.y_size);
    for (ii = 0; ii < MAX_SENSORS; ii++)
        printf("Sensor ID[%d] = %d\n", ii, g_settings.ids[ii]);
    printf("---------------------------------\n\n");

}


unsigned char check_db_health(void)
{
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    int ii;
    //Initialise connection
    conn = mysql_init(NULL);
    
    //Connect to the Database
    printf("Connecting to Database: %s\n",database); 
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        //failed
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }    

    //get values from g_settings table                                 
    printf("Getting values from g_settings\n");
    if (mysql_query(conn, "select * from g_settings where item = 0")) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return EXIT_FAILURE;
    }
    res = mysql_use_result(conn);

    // Store values in g_settings data stucture
    row = mysql_fetch_row(res);
    if (row != NULL)
    {
        printf("Storing values in g_settings struct\n");
        g_settings.num_sensors = atoi(row[1]);
        g_settings.x_size = atoi(row[2]);
        g_settings.y_size = atoi(row[3]);
        g_settings.num_samples = atoi(row[4]);
    }
    else 
    {
        // row corrupt or empty. 
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    mysql_free_result(res);


    //get values from sensors table                                 
    printf("Getting values from sensors\n");
    if (mysql_query(conn, "select * from sensors")) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return EXIT_FAILURE;
    }
    res = mysql_use_result(conn);

    // Store values in g_settings data stucture
    row = mysql_fetch_row(res);
    ii = 0;
    while (row != NULL)
    {
        printf("Storing value in g_settings.ids[%d] struct\n", ii);
        g_settings.ids[ii++] = atoi(row[1]);
        row = mysql_fetch_row(res);
    }
  
    mysql_free_result(res);
    
    // check that the number of sensors in the table is 
    // correct
    if (ii != g_settings.num_sensors)
    {
        printf("Wrong number of sensors in the table\n");
        //Wrong number of sensors in the table; 
        mysql_close(conn);
        return EXIT_FAILURE;
    }    

    //get the number of columns in Main table;
    printf("Getting the number of columns in Main table\n");
    sprintf(query, "SELECT count(*) FROM information_schema.columns WHERE table_schema = '%s' AND table_name = 'Main';",database);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return EXIT_FAILURE;
    }        
    
    res = mysql_use_result(conn);
    
    row = mysql_fetch_row(res);
    if (row != NULL)
    {
        printf("Checking number of columns %d, %d\n", atoi(row[0]), (1 + (g_settings.num_sensors * 2)));
        if ( atoi(row[0]) != (1 + (g_settings.num_sensors * 2)) )
        {
            return EXIT_FAILURE;
            mysql_close(conn);
        }
    }
    else 
    {
        // row corrupt or empty. 
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    mysql_free_result(res);

    //get the number of tables in Database;
    printf("Getting the number of tables in %s\n", database);
    sprintf(query, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = '%s';",database);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return EXIT_FAILURE;
    }        
    
    res = mysql_use_result(conn);
    
    row = mysql_fetch_row(res);
    if (row != NULL)
    {
        printf("Checking number of tables\n");
        if ( atoi(row[0]) != (6 + (g_settings.num_sensors * g_settings.x_size * g_settings.y_size)) )
        {
            mysql_close(conn);
            return EXIT_FAILURE;
        }
    }
    else 
    {
        // row corrupt or empty. 
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    mysql_free_result(res);

    printf("Database Health OK\n");

    mysql_close(conn);

    return EXIT_SUCCESS;
} 


unsigned char populate_main(void)
{
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char query[512];
    //char pos[16];
    int xx, yy, ii;
    int sensor_number = 255;
    int average = 0;
    //Initialise connection
    conn = mysql_init(NULL);
    
    //Connect to the Database
   
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        //failed
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return EXIT_FAILURE;
    }    


    for (xx = 0; xx < g_settings.x_size; xx++)
    {
        for (yy = 0; yy < g_settings.y_size; yy++)
        {
            for (ii = 0; ii < g_settings.num_sensors; ii++)
            {
                sensor_number = g_settings.ids[ii];
                
                //get the average of the rssi's for this sensor at
                //this position
                sprintf(query, "SELECT AVG(rssi) FROM s%02d_pos%02d_%02d;", sensor_number, xx+1,yy+1);
                printf("%s\n", query);
                if (mysql_query(conn, query)) 
                {
                    fprintf(stderr, "%s\n", mysql_error(conn));
                    mysql_close(conn);        
                    return EXIT_FAILURE;
                }        
    
                res = mysql_use_result(conn);
    
                row = mysql_fetch_row(res);
                if (row != NULL)
                {
                    printf("Getting avg\n");
                    average = atoi(row[0]);
                }   
                else 
                {
                    mysql_close(conn);
                    return EXIT_FAILURE;
                }
                
                mysql_free_result(res);

                //insert this value into XXYY in main table

                sprintf(query, "UPDATE Main SET S%02d_rssi = %d WHERE position = %02d%02d;",sensor_number,average,xx+1, yy+1);
                printf("%s\n", query);
                if (mysql_query(conn, query)) 
                {
                    fprintf(stderr, "%s\n", mysql_error(conn));
                    mysql_close(conn);        
                    return 1;
                }   
                
                
            }//for ii
        }//for jj
    }// for kk
    

    return 0;
}
void Monitoring(const char *dev, const int baud, void (*message)(serial_source_msg problem)){
    serial_source fd; //file descriptor
    int ii, kk, ll; // iterators
    int len; //length of the packet read from the forwarder
    uint8_t ph; //placeholder for triangular swap
    uint16_t num_stored = 0; // Number of complete sequences stored 
    unsigned char rpacket[PACKET_SIZE]; //reverse packet.
    unsigned char found_items[g_settings.num_sensors];
    uint16_t search_variable, item; 
    unsigned char num_found = 0; 
    unsigned char * packet; // pointer to the packet read from the
                            // serial forwarder
   
    //--------------------------------------------------
    // Open the serial forwarder and get file descriptor
    //--------------------------------------------------
    fd = open_serial_source(dev,baud, 0, stderr_msg);
    if (!fd)
    {
        fprintf(stderr, "Couldn't open serial port at %s:%d\n",
                dev, baud);
        exit(1);
    }

    //--------------------------------------------------
    // loop through until we get the number of samples we need
    // for each location
    // --------------------------------------------------------
    while(num_stored < g_settings.num_samples)
    {
        // Read Packet
        //------------
        packet = read_serial_packet(fd, &len);
        
        if (!packet)
            exit(0);
        //make a copy
        memcpy(rpacket, packet, len);
        
        //triangular swap for wrong endianness.
        //------------------------------------
        ph = rpacket[18];
        rpacket[18] = rpacket[17];
        rpacket[17] = ph; 
       
        ph = rpacket[20];
        rpacket[20] = rpacket[19];
        rpacket[19] = ph;
        
        // build Struct from packet read
        // -----------------------------
        Rec_Packet.source_id = rpacket[SOURCE_ID];
        Rec_Packet.rssi = rpacket[RSSI];
        memcpy(&Rec_Packet.seq_no, &rpacket[SEQ_NO], 2);
        memcpy(&Rec_Packet.reading, &rpacket[READING], 2);

        // We are only looking for specific packets
        if (len == PACKET_SIZE)
        {
            //Validate Addresses (dont need base or target) 
            if (Rec_Packet.source_id != 0x00 &&
                Rec_Packet.source_id != 0x08)  
            {               
                //shift right buffer. 
                //printf("shifting right...\n");
                for (kk = BUFFER_SIZE-1; kk > 0; kk--)
                {
                    memcpy(&Packet_Buffer[kk], 
                           &Packet_Buffer[kk-1], sizeof(struct Packet));
                }
      
                // put the read value into the first element of the
                // buffer

                memcpy(&Packet_Buffer[0], &Rec_Packet, sizeof(struct Packet));


                // Find how many instances of each sequence number in
                // the buffer there are. If there are the name amount
                // of instances as there are sensors, then we have a
                // full suite and we can store these instances in the DB
                search_variable = Packet_Buffer[0].seq_no;
                //printf("searching for %04u\n", search_variable);
                for (ll = 0; ll < BUFFER_SIZE; ll++)
                {
                    item = Packet_Buffer[ll].seq_no;
                           
                    if (item == search_variable)
                    {
                        num_found++;
                                
                        found_items[num_found-1] = ll; //points
                        //to element
                        fflush(stdout); 
                                
                                
                        if (num_found == g_settings.num_sensors)
                        {
                            // we have a full set, 
                            num_stored++;

                            for (ii = 0; ii < g_settings.num_sensors; ii++)
                            {
                                
                                store_monitor(Packet_Buffer[found_items[ii]], num_stored); 
                                
                            }       
                        }// if (num_found... 
                    }// if item == search variable...
                }//for ll = 0 ...
                num_found = 0;
            } //validate addresses
        }// if len == packet_size

        free((void *)packet);
    }//while num_stored
    num_stored = 0; // reset for next loop
 
}


unsigned char store_monitor(struct Packet P, int sample)
{
    MYSQL *conn;
     
    char query[512];

    conn = mysql_init(NULL);

    //Connect to the Database
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }


    sprintf(query, "UPDATE monitor SET S%02d = %d WHERE sample = %02d;", P.source_id, P.rssi, sample);
    printf("%s\n", query);
    if (mysql_query(conn, query)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }   


    return 0;

}

unsigned char get_position(void) 
{
    // Algorithm
    //----------
    //1. Calculate the difference between the recieved RSSI's from the
    //sensors and each value in the Main table
    //2. Store these differences in a new table with and ADDED column
    //called TOTAL. There shall be a column holding the primary key of
    //the Main table's row. 
    //3. Calculate the SUM of each row in the new table.
    //4. Calculate the Minimum of the SUM row and return the key to
    //the main table (this will be the closest position);

    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char  suf[] =  "FROM monitor";    
   
    char middle1[512];
    char temp1[128];
    int ll;
    uint16_t avgs[g_settings.num_sensors];

    conn = mysql_init(NULL);

    //Connect to the Database
    if (!mysql_real_connect(conn, server,
                            user, password, database, 0, NULL, 0)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);        
        return 1;
    }

        
    sprintf(middle1, "SELECT AVG(S%02d)", g_settings.ids[0]);
    for (ll = 1; ll < g_settings.num_sensors; ll++)
    {
        sprintf(temp1, ",AVG(S%02d) ", g_settings.ids[ll]);
        strcat(middle1, temp1);
    }
    strcat(middle1, suf);
    printf("%s\n", middle1);
    if (mysql_query(conn, middle1)) 
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        mysql_close(conn);
        return 1;
    }
    

    res = mysql_use_result(conn);
    
    
    row = mysql_fetch_row(res);
    if (row != NULL)
    {
        for (ll = 0; ll < g_settings.num_sensors; ll++)
        {
            avgs[ll] = atoi(row[ll]);
            printf("%d, ", avgs[ll]);

        }
        printf("\n");
    }
    else 
    {
        // row corrupt or empty. 
        mysql_close(conn);
        return EXIT_FAILURE;
    }

    mysql_free_result(res);




    return 0;

}


