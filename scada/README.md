# Supervisório – Elipse SCADA

Aplicação do sistema supervisório do forno de torrefação, desenvolvida no Elipse SCADA (versão Demo v2.29, build 151). Ela se comunica com o Arduino por Modbus e permite acompanhar a temperatura, o estado da resistência e o tempo restante, além de configurar e iniciar ou cancelar a torra pelo computador.

## Arquivos

- `TorrefacaoAmendoasCacau.app`: aplicação do Elipse SCADA.
- `novas imagens com risquinho/feita no canva/`: imagens dos termômetros usadas nas telas (`termometro_01.jpg` a `termometro_15.jpg`). Essa pasta precisa ficar ao lado do `.app` para as imagens aparecerem.

## Como abrir

1. Instale o Elipse SCADA.
2. Baixe esta pasta inteira e abra o arquivo `.app` no Elipse SCADA.
3. Configure o driver de comunicação:
   - Driver: Modicon Modbus v4.0.3 (`modbus.dll`), disponível no site da Elipse. O driver não está incluído neste repositório.
   - Aponte o driver para o local do `modbus.dll` no seu computador.
   - Porta serial: a porta COM em que o Arduino está conectado, a 9600 bps.
   - Endereço do escravo: 1.

A lista de tags e dos registradores Modbus está no [README principal](../README.md#comunicação-modbus-scada).
