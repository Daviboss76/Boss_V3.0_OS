# 🚀 BossOS (32-bit Kernel)
**Um Sistema Operacional de 32 bits feito do zero, com bootloader próprio, driver IDE, sistema de arquivos FAT32 e interface gráfica VGA.**

Este é um projeto avançado de arquitetura de computadores e desenvolvimento de sistemas operacionais bare-metal, construído e programado inteiramente em ambiente mobile via **Termux** e testado em emuladores x86.

## 🛠️ Características Principais:
- **Arquitetura:** 32-bit Protected Mode (x86).
- **Linguagens:** C e Assembly x86 (NASM).
- **Tamanho/Escala:** Mais de 1.500 linhas de código modularizadas em C, com bootloader em Assembly.
- **Gerenciamento de Armazenamento:** Driver IDE nativo e suporte a leitura de sistema de arquivos **FAT32**.
- **Interface Gráfica & UI:** Animação de inicialização (Splash Screen) em modo gráfico VGA, terminal interativo e gerenciador de comandos.
- **Subsistemas:** Drivers de Vídeo (VGA), Teclado, RTC (Relógio), Timer e barramento PCI.

## 📁 Estrutura do Projeto:
- `mbr.asm` & `stage2.asm`: O carregador de boot de múltiplos estágios.
- `kernel/`: O núcleo de orquestração do sistema (`kernel.c`).
- `drivers/`: Drivers de baixo nível (`vga.c`, `ide.c`, `keyboard.c`, `pci.c`, `rtc.c`, `timer.c`).
- `fs/`: Implementação do sistema de arquivos (`fat32.c`).
- `ui/`: Interface de usuário, terminal e animações de boot (`terminal.c`, `splash.c`, `ui.c`).
- `include/`: Arquivos de cabeçalho (`.h`) do kernel e dos drivers.
- `lib/`: Biblioteca utilitária padrão do kernel (`lib.c`).

## 💻 Como Compilar e Rodar:
O projeto utiliza uma cadeia de ferramentas padrão (`gcc`, `ld`, `nasm`) e pode ser executado em emuladores compatíveis com arquitetura x86 (como **QEMU**, **Bochs** ou **Limbo x86**):

```bash
# Compilação e geração da imagem do disco
make

# Executando no QEMU
qemu-system-i386 -fda disk.img

