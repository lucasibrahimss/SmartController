[README.md](https://github.com/user-attachments/files/29297118/README.md)
# Firmware do Home Control Device — ESP32

Firmware desenvolvido para um dispositivo de controle residencial acessível baseado em **ESP32**, com sete botões físicos, display LCD 16×2 via I²C, comunicação Wi-Fi/MQTT, relógio sincronizado por NTP, portal web de configuração e exibição de lembretes de medicamentos.

## 1. Visão geral

O firmware integra o dispositivo físico ao **Home Assistant** por meio de um broker MQTT. Os botões publicam eventos em tópicos individuais, enquanto o ESP32 também recebe mensagens para o display e lembretes de medicamentos.

Principais funcionalidades:

- leitura de sete botões físicos;
- identificação de pressão curta e pressão longa;
- publicação dos eventos dos botões via MQTT;
- recepção de mensagens temporárias para o LCD;
- recepção e apresentação cíclica de lembretes de medicamentos;
- relógio sincronizado por NTP;
- controle automático do backlight;
- configuração persistente de Wi-Fi e MQTT;
- portal web de configuração em modo Access Point;
- publicação de estado online/offline por MQTT;
- reconexão automática ao Wi-Fi e ao broker MQTT.

## 2. Hardware utilizado

- ESP32;
- display LCD 16×2 com adaptador I²C;
- sete botões físicos;
- alimentação compatível com o ESP32 e o display;
- rede Wi-Fi de 2,4 GHz;
- servidor Home Assistant com broker MQTT.

### 2.1 Mapeamento dos pinos

| Componente | GPIO do ESP32 |
|---|---:|
| Botão 1 | GPIO23 |
| Botão 2 | GPIO25 |
| Botão 3 | GPIO26 |
| Botão 4 | GPIO27 |
| Botão 5 | GPIO32 |
| Botão 6 | GPIO4 |
| Botão 7 | GPIO33 |
| LCD SDA | GPIO21 |
| LCD SCL | GPIO22 |

Os botões devem ser conectados entre o respectivo GPIO e o GND. O firmware utiliza `INPUT_PULLUP`, portanto:

- botão solto: nível lógico `HIGH`;
- botão pressionado: nível lógico `LOW`.

O display é inicializado no endereço I²C `0x27`, com 16 colunas e 2 linhas.

## 3. Ambiente de desenvolvimento

Configuração recomendada:

| Item | Configuração |
|---|---|
| IDE | Arduino IDE |
| Placa | ESP32 Dev Module |
| Monitor serial | 115200 bit/s |
| Upload | 115200 bit/s, quando necessário para maior estabilidade |
| Barramento I²C | SDA = GPIO21 e SCL = GPIO22 |

## 4. Dependências

### 4.1 Bibliotecas externas

Instalar pelo Gerenciador de Bibliotecas da Arduino IDE:

- `LiquidCrystal_I2C`;
- `PubSubClient`, de Nick O'Leary.

### 4.2 Bibliotecas fornecidas pelo pacote ESP32

As bibliotecas abaixo fazem parte da plataforma ESP32 instalada pelo Gerenciador de Placas:

- `Wire.h`;
- `WiFi.h`;
- `time.h`;
- `Preferences.h`;
- `WebServer.h`.

### 4.3 Inclusões do firmware

```cpp
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <Preferences.h>
#include <WebServer.h>
```

## 5. Configuração inicial

As configurações de Wi-Fi e MQTT são armazenadas na memória não volátil do ESP32 por meio da biblioteca `Preferences`, no namespace:

```text
appcfg
```

São armazenados os seguintes parâmetros:

- SSID da rede Wi-Fi;
- senha da rede Wi-Fi;
- endereço ou IP do broker MQTT;
- porta MQTT;
- usuário MQTT;
- senha MQTT.

Ao iniciar, o firmware carrega os dados salvos. Caso não exista um SSID configurado ou a conexão Wi-Fi falhe, o ESP32 abre automaticamente um portal de configuração.

## 6. Portal web de configuração

Quando o Wi-Fi não pode ser utilizado, o ESP32 cria a rede:

```text
SSID: ESP32-Remedios-Setup
Senha: 12345678
```

O endereço IP do portal é mostrado no display LCD. Normalmente, o endereço de um Access Point do ESP32 é `192.168.4.1`, mas deve-se utilizar o IP efetivamente mostrado na tela.

Procedimento:

1. ligar o dispositivo;
2. aguardar a mensagem `Modo Config` no LCD;
3. conectar um celular ou computador à rede `ESP32-Remedios-Setup`;
4. abrir no navegador o endereço IP mostrado no LCD;
5. preencher os campos de Wi-Fi e MQTT;
6. selecionar `Salvar e reiniciar`.

O formulário permite configurar:

- Wi-Fi SSID;
- Wi-Fi senha;
- MQTT host/IP;
- MQTT porta;
- MQTT usuário;
- MQTT senha.

Também existe a opção `Apagar configuração salva`, que limpa os dados persistidos e reinicia o ESP32.

### Observação

Na versão atual, o portal é aberto automaticamente quando não existe um SSID salvo ou quando a conexão Wi-Fi falha. Uma falha exclusiva do MQTT não abre o portal; nesse caso, o firmware continua tentando reconectar ao broker.

## 7. Conexão Wi-Fi

O firmware tenta conectar-se à rede configurada por até 15 segundos.

Durante a tentativa, o LCD mostra:

```text
WiFi:
Conectando...
```

Em caso de sucesso:

- o endereço IP local é mostrado no LCD;
- o relógio é sincronizado por NTP;
- o firmware inicia a conexão com o broker MQTT.

Durante a operação, o estado do Wi-Fi é verificado a cada 5 segundos. Se a conexão cair, o firmware tenta reconectar. Se a reconexão falhar, o portal de configuração é iniciado.

## 8. Comunicação MQTT

### 8.1 Configurações padrão

O firmware possui os seguintes valores padrão:

```text
Broker padrão: 192.168.3.30
Porta padrão: 1883
Cliente base: esp32_tcc_remoto
```

O identificador MQTT é complementado por um valor aleatório, reduzindo o risco de conflito entre clientes.

As credenciais e o endereço do broker podem ser alterados pelo portal web.

### 8.2 Disponibilidade do dispositivo

O firmware publica o estado do ESP32 no tópico:

```text
tcc/remoto/status
```

Payloads:

- `online`: publicado após conexão bem-sucedida;
- `offline`: configurado como Last Will and Testament do cliente MQTT.

A mensagem de disponibilidade é retida pelo broker.

Também é publicada uma mensagem de inicialização em:

```text
tcc/remoto/debug
```

Payload:

```text
ESP32 iniciou
```

## 9. Tópicos MQTT dos botões

Cada botão publica em um tópico próprio:

| Botão | Tópico |
|---|---|
| Botão 1 | `tcc/lucas/button1` |
| Botão 2 | `tcc/lucas/button2` |
| Botão 3 | `tcc/lucas/button3` |
| Botão 4 | `tcc/lucas/button4` |
| Botão 5 | `tcc/lucas/button5` |
| Botão 6 | `tcc/lucas/button6` |
| Botão 7 | `tcc/lucas/button7` |

### 9.1 Pressão curta

Quando o botão é pressionado e solto antes de 3 segundos, o firmware publica:

```text
on
```

O LCD mostra o número do botão e a mensagem:

```text
apertado
```

### 9.2 Pressão longa

Quando o botão permanece pressionado por 3 segundos ou mais, o firmware publica:

```text
5sec
```

Apesar do payload manter o nome `5sec`, o limiar utilizado pela versão atual do firmware é de **3 segundos**. O nome foi preservado por compatibilidade com as automações MQTT já configuradas.

Para os botões 1 a 6, o LCD mostra:

```text
por 3 sec
```

No caso do Botão 7, a pressão longa encerra a exibição do lembrete de medicamentos e limpa o LCD sem mostrar a mensagem padrão do botão.

## 10. Tópicos assinados pelo ESP32

O ESP32 assina dois tópicos:

```text
tcc/lucas/lcd
tcc/lucas/remedios
```

### 10.1 Mensagens temporárias

Tópico:

```text
tcc/lucas/lcd
```

O payload deve ser uma mensagem de texto. O firmware:

1. divide o texto em até duas linhas de 16 caracteres;
2. liga o backlight;
3. mostra a mensagem por 2 segundos;
4. limpa o display.

Exemplo:

```text
Tópico: tcc/lucas/lcd
Payload: Luz da sala ligada
```

Textos acima de 32 caracteres são truncados.

### 10.2 Lembretes de medicamentos

Tópico:

```text
tcc/lucas/remedios
```

O payload deve ser enviado em formato JSON.

Exemplo:

```json
{
  "hora": "20:00",
  "remedios": [
    {
      "nome": "Medicamento A",
      "dose": "1 comprimido"
    },
    {
      "nome": "Medicamento B",
      "dose": "10 ml"
    }
  ]
}
```

O firmware procura no JSON:

- o campo `hora`;
- até dez ocorrências de `nome`;
- até dez ocorrências correspondentes de `dose`.

O display mostra:

- na primeira linha: horário e posição do item, por exemplo `20:00 1/2`;
- na segunda linha: nome do medicamento.

Quando existem vários medicamentos, o firmware alterna entre eles a cada 3 segundos.

### Observação sobre o campo `dose`

O campo `dose` é interpretado e armazenado pelo firmware, mas a versão atual apresenta no LCD apenas o nome do medicamento. A dose permanece disponível internamente para futuras evoluções da interface.

### Limites

- quantidade máxima: 10 medicamentos;
- nome mostrado no LCD: até 16 caracteres;
- horário ausente: substituído por `--:--`;
- payload sem medicamentos válidos: mostra `Sem remedios`.

## 11. Comportamento do display

O LCD possui três comportamentos principais.

### 11.1 Relógio em repouso

Quando não existe uma mensagem ativa, o display mostra o horário no formato:

```text
....HH:MM:SS....
```

O relógio é atualizado a cada segundo sem ligar automaticamente o backlight.

### 11.2 Mensagem temporária

As mensagens recebidas em `tcc/lucas/lcd` permanecem por 2 segundos e depois são apagadas.

### 11.3 Lembrete de medicamento

As mensagens recebidas em `tcc/lucas/remedios` permanecem ativas até serem substituídas ou interrompidas por uma interação.

Ao pressionar qualquer botão, o modo de lembrete é encerrado para permitir a execução da nova ação. A pressão longa do Botão 7 possui tratamento específico para limpar diretamente o lembrete.

## 12. Controle do backlight

O firmware controla o backlight por software.

Tempos configurados:

| Situação | Timeout |
|---|---:|
| Uso normal | 15 segundos |
| Lembrete de medicamento | 60 segundos |

Após o timeout, apenas o backlight é desligado. O conteúdo do LCD pode continuar visível sem iluminação.

### Observação técnica

Quando há mais de um medicamento, a troca de item a cada 3 segundos utiliza a função de atualização do LCD, que também renova o instante da última atividade. Portanto, na implementação atual, a rotação contínua de vários medicamentos pode manter o backlight ligado além dos 60 segundos previstos.

## 13. Relógio e sincronização NTP

O firmware utiliza:

```text
Servidor NTP: pool.ntp.org
Fuso horário: UTC-3
Horário de verão: desabilitado
```

A sincronização é realizada depois que o Wi-Fi é conectado.

Se o dispositivo perder energia, o relógio é recuperado novamente pela internet na próxima inicialização. Não é utilizado um módulo RTC externo.

Caso a sincronização NTP falhe, o relógio não é mostrado até que uma nova sincronização seja realizada.

## 14. Fluxo de inicialização

1. inicialização da comunicação serial;
2. inicialização do barramento I²C;
3. inicialização do LCD;
4. configuração dos sete botões;
5. carregamento das configurações persistentes;
6. tentativa de conexão Wi-Fi;
7. abertura do portal de configuração, se necessário;
8. sincronização do relógio por NTP;
9. conexão com o broker MQTT;
10. publicação do estado `online`;
11. assinatura dos tópicos do LCD e dos medicamentos;
12. entrada no ciclo principal de funcionamento.

## 15. Fluxo principal

Durante a operação, o firmware executa continuamente:

1. atendimento do portal de configuração, quando ativo;
2. verificação da conexão Wi-Fi;
3. verificação da conexão MQTT;
4. processamento das mensagens MQTT;
5. atualização cíclica dos medicamentos;
6. leitura e tratamento dos botões;
7. atualização do relógio;
8. gerenciamento do timeout do backlight.

## 16. Compilação e gravação

1. instalar o pacote `esp32 by Espressif Systems` no Gerenciador de Placas;
2. instalar `LiquidCrystal_I2C`;
3. instalar `PubSubClient`;
4. selecionar `ESP32 Dev Module`;
5. conectar o ESP32 ao computador;
6. selecionar a porta serial correspondente;
7. compilar;
8. realizar o upload.

Em caso de falha de comunicação durante o upload:

- reduzir a velocidade para 115200 bit/s;
- fechar o Monitor Serial;
- verificar o cabo USB;
- pressionar e manter o botão `BOOT` durante a etapa `Connecting...`;
- desconectar temporariamente os periféricos, se necessário.

## 17. Testes MQTT

Para observar todos os tópicos do projeto, assinar:

```text
tcc/#
```

Exemplos de teste:

### Mensagem temporária

```text
Tópico: tcc/lucas/lcd
Payload: Teste do display
```

### Lembrete

```text
Tópico: tcc/lucas/remedios
Payload: {"hora":"08:00","remedios":[{"nome":"Medicamento A","dose":"1 comprimido"}]}
```

### Eventos dos botões

```text
tcc/lucas/button1 = on
tcc/lucas/button1 = 5sec
```

## 18. Considerações de segurança

A versão atual possui valores padrão de MQTT e uma senha fixa para o Access Point de configuração. Para publicação em um repositório aberto ou utilização fora do ambiente de testes, recomenda-se:

- alterar a senha do Access Point;
- remover credenciais reais do código-fonte;
- utilizar credenciais MQTT exclusivas para o dispositivo;
- restringir o acesso ao broker;
- avaliar o uso de MQTT com TLS;
- evitar redes Wi-Fi públicas;
- não publicar exportações da memória NVS.

As informações inseridas pelo portal são armazenadas na memória do ESP32. O portal utiliza HTTP sem criptografia e deve ser usado apenas em uma rede controlada.

## 19. Limitações conhecidas

- o parser de JSON foi implementado manualmente e não trata todos os formatos JSON possíveis;
- aspas escapadas dentro dos textos podem não ser interpretadas corretamente;
- o campo `dose` ainda não é mostrado no LCD;
- o portal de configuração é iniciado automaticamente apenas em falhas de Wi-Fi;
- falhas do MQTT resultam em novas tentativas periódicas, sem ativar o portal;
- a versão atual não implementa deep sleep;
- os métodos que utilizam `delay()` interrompem temporariamente outras tarefas;
- a rotação de vários medicamentos pode renovar continuamente o timeout do backlight;
- somente um botão é tratado por vez;
- textos acima da capacidade do LCD são truncados.

## 20. Melhorias futuras

Possíveis evoluções:

- utilizar a biblioteca ArduinoJson;
- exibir nome e dose de cada medicamento;
- permitir acesso manual ao portal por combinação de botões;
- implementar deep sleep em falhas prolongadas de rede;
- substituir `delay()` por temporização não bloqueante;
- implementar debounce por software;
- utilizar TLS na comunicação MQTT;
- disponibilizar página web para diagnóstico;
- permitir atualização remota do firmware;
- adicionar confirmação explícita de medicamento tomado;
- armazenar o último lembrete para recuperação após reinicialização.

## 21. Licença

Defina no repositório a licença adequada ao projeto. Caso ainda não exista uma licença, o código permanece protegido por direitos autorais e não deve ser considerado automaticamente livre para reutilização.
