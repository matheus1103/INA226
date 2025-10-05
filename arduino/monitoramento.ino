/*
 * INA226 - MONITOR CONTÍNUO AUTOMÁTICO
 * Versão sem inputs - apenas transmite dados continuamente
 * SHUNT: R100 = 0.1Ω (100mΩ)
 */

#include <Wire.h>
// Biblioteca INA226.h do https://github.com/RobTillaart/INA226
#include "INA226.h"

INA226 INA(0x44);

// Configuração do Shunt R100
const float SHUNT_RESISTANCE = 0.1;       // 100mΩ (R100)
const float MAX_CURRENT = 0.5;            // 500mA máximo
const float CORRECTION_FACTOR = 0.968;    // Fator de correção calibrado

// Variáveis globais
unsigned long sessionStartTime = 0;
unsigned long sampleCount = 0;
float baselineCurrent = 0;
float totalEnergy = 0;

// Intervalo de transmissão (ms)
const unsigned long TRANSMISSION_INTERVAL = 50;  // 20Hz
unsigned long lastTransmission = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);  // Aguardar estabilização
    
    // Header inicial
    Serial.println("# INA226 ESP32C6 Monitor - Continuous Mode");
    Serial.println("# Version: 1.0");
    Serial.println("# Shunt: 100mOhm (R100)");
    Serial.println("# Correction Factor: 0.968");
    
    // Inicializar I2C
    Wire.begin(21, 22);
    Wire.setClock(400000);
    
    // Verificar INA226
    if (!INA.begin()) {
        Serial.println("# ERROR: INA226 not detected!");
        while(1) {
            Serial.println("# ERROR: Check connections SDA=GPIO21 SCL=GPIO22");
            delay(5000);
        }
    }
    
    // Configurar INA226
    int result = INA.setMaxCurrentShunt(MAX_CURRENT, SHUNT_RESISTANCE, true);
    if (result != 0x0000) {
        Serial.printf("# Warning: Config result 0x%04X\n", result);
    }
    
    // Modo otimizado para monitoramento contínuo
    INA.setAverage(INA226_4_SAMPLES);              // 4 amostras para balanço
    INA.setBusVoltageConversionTime(INA226_588_us);    // 588µs
    INA.setShuntVoltageConversionTime(INA226_588_us);  // 588µs
    INA.setModeShuntBusContinuous();
    
    Serial.printf("# Current LSB: %.6f mA\n", INA.getCurrentLSB_mA());
    Serial.printf("# Power LSB: %.6f mW\n", INA.getCurrentLSB_mA() * 25);
    
    // Medir baseline (média de 2 segundos)
    Serial.println("# Measuring baseline...");
    measureBaseline();
    Serial.printf("# Baseline current: %.2f mA\n", baselineCurrent);
    
    // Informações do sistema
    Serial.printf("# Manufacturer ID: 0x%04X\n", INA.getManufacturerID());
    Serial.printf("# Die ID: 0x%04X\n", INA.getDieID());
    Serial.printf("# Free RAM: %d bytes\n", ESP.getFreeHeap());
    
    // Inicializar sessão
    sessionStartTime = millis();
    sampleCount = 0;
    
    // Cabeçalho CSV
    Serial.println("# Starting continuous monitoring...");
    Serial.println("# Format: Timestamp(ms),Voltage(V),Current(mA),CurrentNet(mA),Power(mW),Energy(mWh)");
    Serial.println("# DATA_START");
}

void measureBaseline() {
    float sum = 0;
    int samples = 0;
    unsigned long startTime = millis();
    
    // Coletar amostras por 2 segundos
    while(millis() - startTime < 2000) {
        if(INA.isConversionReady()) {
            float current = abs(INA.getCurrent_mA()) * CORRECTION_FACTOR;
            sum += current;
            samples++;
            delay(10);
        }
    }
    
    if(samples > 0) {
        baselineCurrent = sum / samples;
    } else {
        baselineCurrent = 0;
    }
}

void loop() {
    // Verificar se é hora de transmitir
    unsigned long currentMillis = millis();
    
    if(currentMillis - lastTransmission >= TRANSMISSION_INTERVAL) {
        if(INA.isConversionReady()) {
            // Ler valores
            float voltage = INA.getBusVoltage();
            float current = abs(INA.getCurrent_mA()) * CORRECTION_FACTOR;
            float power = voltage * current;
            float netCurrent = current - baselineCurrent;
            
            // Timestamp relativo ao início da sessão
            unsigned long timestamp = currentMillis - sessionStartTime;
            
            // Atualizar contador
            sampleCount++;
            
            // Calcular energia incremental (mWh)
            float deltaTime = (currentMillis - lastTransmission) / 1000.0 / 3600.0;  // horas
            totalEnergy += power * deltaTime;
            
            // Transmitir dados em formato CSV com alta precisão
            Serial.printf("%lu,%.6f,%.4f,%.4f,%.4f,%.6f\n",
                         timestamp,
                         voltage,
                         current,
                         netCurrent,
                         power,
                         totalEnergy);
            
            lastTransmission = currentMillis;
        }
    }
}