#include <stdio.h>
#include <windows.h>
#include <stdint.h>
#include <conio.h>

#define MAXMEASUREMENTS 20
#define COMMAND_LENGTH 255
//vafan

// Serial ports är till för kommunikation med externa devices, DB9 var dm gamla med 9 pins men används inom industrin, UART
// Namnges genom tex COM1, COM2, COM3. Double digits --> Serial converters
// Enhetshanteraren --> Portar visar anslutna enheter
// DCB eller device control block är en struct i windows API:n som beskriver inställningarna för en seriell kommunikationsport
// Windows behöver veta tex Baudrate, databitar, stop bits osv. Vi skapar en variabel av typen DCB(STRUCT) och fyller med inställningar 
// Sedan skickar vi en pekare till windows till just denna DCB, Vi skickar pekarne med SetCommstate funktionen och getcom ger nuvarande inställningar
// Getcomm --> Fyller structen som har väldigt många saker med förbestämda inställningar och sedan ändrar vi vissa saker 


typedef struct PlotSettings
{
    char fileName[COMMAND_LENGTH];
    char title[COMMAND_LENGTH];
    char style[COMMAND_LENGTH];
} PlotSet;

typedef struct GnuSettings
{
    char title[COMMAND_LENGTH];
    char xLabel[COMMAND_LENGTH];
    char yLabel[COMMAND_LENGTH];
    PlotSet plot;
} GnuSet;


typedef struct {
    uint32_t time_ms;
    double ax;
    double ay;
    double az;
    double gx;
    double gy;
    double gz;
    double a_total;
} IMUdata;

void saveMeasurments(IMUdata measurements[], int currentMeasurements, char fileName[]);

int main(void) {
    DWORD win32_error_code; // DWORD är en heltalstyp i windows api och getlasterorr returnerar en DWORD. Och är typ en 32 bitars integer, DOUBLE WORD OCH WINDOWS ÄLSKAR DEN
    char error_message[256]; // Lagrrar felmeddelandet i text.
    IMUdata measurements[MAXMEASUREMENTS];
    int currentMeasurements = 0;
    HANDLE hComm;

    GnuSet plotSettings = {
        .title = "set title \"Serious Head Injuries\"",
        .xLabel = "set xlabel \"Time\"",
        .yLabel = "set ylabel \"Force\"",
        .plot = {
            .fileName = "measurements.txt",
            .title = "This is our graph",
            .style = "with lines"
        }
    };
    FILE *gnupipe = NULL;
    gnupipe = _popen("gnuplot -persistent", "w");

    hComm = CreateFileA("\\\\.\\COM3",          // A står för ANSI version medan om man lägger W så är det UNICODE version, 
                        GENERIC_READ,    // Generic read, dvs vi vill läsa från porten
                        0,             // Ingen delning av filen
                        NULL,        // Ingen säkerhet/encryption
                        OPEN_EXISTING,        // Öppnar en existerande port
                        0,      // Non overlapped io --> En process i taget
                        NULL);          // Null --> Kommunicerande enheter 

    if (hComm == INVALID_HANDLE_VALUE) {
        printf("Error in opening serial port\n");  // Win32 har massa erorr codes --> 0-15999 som kan indikera varför något inte funkar, kan kalla på getLASTerror som returnerar senaste error code

        // Får fram error code
        win32_error_code = GetLastError();  // Returnerar error code

        // Översätter error code till något läsbart
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
                        NULL,
                        win32_error_code,
                        0,
                        error_message,
                        sizeof(error_message),
                        NULL
                    );
        printf("Error is: %s", error_message);        // Om porten/enheten inte är ansluten så kommer man få "det går inte att hitta filen"

    } else {                                           // Eller FormatMessage som överästter error code till ett läsbart meddelande, så getlasterror--> formatmesage
        printf("Opening serial port succesful\n");
    }

    // Konfigueringen av DCB structen för serial kommunikation
    DCB DCB_Struct_Parameter = {0};   // Sätter allt i structen till 0
    DCB_Struct_Parameter.DCBlength = sizeof(DCB_Struct_Parameter);   // Den har en inbyggd lebngth inuti sig 
    BOOL status = GetCommState(hComm, &DCB_Struct_Parameter);    // Hämta nuvarande inställningar och returnera om det lyckades eller inte, laddar även structen med nuvarande inställningar
    if (status == FALSE) {
        printf("\nError in GetCommState()");
    } else {
        printf("\nGetCommState success");
    }

    DCB_Struct_Parameter.BaudRate = CBR_115200;  // Kommunikationshastigheten, dvs antalet symboler per sekund 
    DCB_Struct_Parameter.ByteSize = 8;          // Varje data som skickas innehåller 8 bitar 
    DCB_Struct_Parameter.Parity = NOPARITY;      // Feldetekering, inga extra bits läggs till 
    DCB_Struct_Parameter.StopBits = ONESTOPBIT;    // Markerar slutet på en RAM

    status = SetCommState(hComm, &DCB_Struct_Parameter); 

    if (status == FALSE) {
        printf("\nError in SetCommState");
    } else {
        printf("\nSetCommState success\n");
    }

    PurgeComm(hComm, PURGE_RXCLEAR); // Rensar all gammal data som finns i recieve bufferten så bara ny data som skickas kommer till mig. TXCLEAR för transmit buffert

    /////// Inläsning /////
    // ReadFile(Function)  handle--> adress till serial porten, returnerar en bool dvs true or false
    // LPvoid --> Pointer till bufferten som tar emot datan
    // DWORD --> Antal bytes som ska läsas
    // LPDWORD --> Pointer till variabel som ska ta emot datan
    //  LPoverlapped --> Pointer till en overlapped struct för asynkron i/o
    BOOL success;
    DWORD bytesRead;
    char receive_data_buffer[COMMAND_LENGTH] = {0};
    char lineBuffer[COMMAND_LENGTH];          // Temorär sträng för att bygga upp en hel "sträng"
    int linePosition = 0;       // Håller koll på vilken char vi är på så om vi tar emot hälften av datan så kan resterande delen appendas till samma sträng
    // Tiemouts?? Kolla på senare
    // Blocking --> Programmet inväntar data
    // Non blocking --> Programmet gör andra saker medan den väntar på data
    // 2 strategier finns, "polling" som kollar efter data, eller "event driven" mha WaitCommEvent som triggas när data kommer
    while (currentMeasurements < MAXMEASUREMENTS) {
        success = ReadFile(hComm,                 // Adress till öppnad port
                            receive_data_buffer,          // Poitner till buffeert
                            sizeof(receive_data_buffer) - 1,    // Storlek på datan
                            &bytesRead,                  // Pointer till variabel som tar emot bytes
                            NULL);

        if (success && bytesRead > 0) {                      // Kolla på \n inmatning för högre sampling rate senare 
            for (DWORD i = 0; i < bytesRead; i++) {
                char currentChar = receive_data_buffer[i];
                if (currentChar != '\n') {
                    lineBuffer[linePosition] = currentChar;
                    linePosition++;
                } else {
                    lineBuffer[linePosition] = '\0';
                    int dataRead = sscanf(lineBuffer,         // Omvandlar värden i textformat till faktiska numeriska värden, returnerar även hur många värden den läser in
                                "%u;%lf;%lf;%lf;%lf;%lf;%lf;%lf",
                                &measurements[currentMeasurements].time_ms,
                                &measurements[currentMeasurements].ax,
                                &measurements[currentMeasurements].ay,
                                &measurements[currentMeasurements].az,
                                &measurements[currentMeasurements].gx,
                                &measurements[currentMeasurements].gy,
                                &measurements[currentMeasurements].gz,
                                &measurements[currentMeasurements].a_total);
                    printf("dataRead = %d\n", dataRead);
                    if (dataRead == 8) {
                        currentMeasurements++;
                    }
                    linePosition = 0;
                }
            }
        }
        
        if (kbhit()) {     // Kollar om en tangent har trycks
            char input = getch();                 // Hämtar tangenten som tryckts
            if (input == 'q' || input == 'Q') {              // Om q --> avslutar programmet och sparar 
                break;
            }
        }
    }

    printf("Data collection is complete\n");
    //sscanf returnerar hur många värden den lyckades läsa och vi vill läsa in 8 
    saveMeasurments(measurements, currentMeasurements, plotSettings.plot.fileName);
    CloseHandle(hComm); // Closign serial Port, behövs för att om man inte stängger kommunikation kan itne andra program använda porten
    
    //Execute gnucommands
    fprintf(gnupipe, "%s\n", plotSettings.title);
    fprintf(gnupipe, "%s\n", plotSettings.xLabel);
    fprintf(gnupipe, "%s\n", plotSettings.yLabel);
    fprintf(gnupipe, "plot '%s' %s title '%s'\n", plotSettings.plot.fileName, plotSettings.plot.style, plotSettings.plot.title);
    _pclose(gnupipe);
    return 0;
}

void saveMeasurments(IMUdata measurements[], int currentMeasurements, char fileName[]) {
    FILE *pFile;
    if (pFile = fopen(fileName, "w")) {
        for (int i = 0; i < currentMeasurements; i++) {
            fprintf(pFile, "%u %f\n", 
                    measurements[i].time_ms,
                    measurements[i].a_total);
        }
        printf("Saving to %s\n", fileName);
        fclose(pFile);
    } else {
        printf("Problems with the saving of the file\n");
    }
}
