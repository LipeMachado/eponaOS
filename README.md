# EponaOS

Este repositório contém o código do sistema operacional EponaOS.

## Pré-requisitos

O projeto requer um ambiente Unix-like. Se você estiver usando Windows, existem várias maneiras de configurar um (WSL, uma máquina virtual Linux, Cygwin, MSYS2). Recomendo usar o WSL, que é o mais fácil de configurar.

Para este projeto, você precisa das seguintes ferramentas:

- `make`
- `nasm` (Netwide Assembler)
- `qemu-system-x86` para testes
- `bochs-x bochsbios vgabios` para debug
- seu editor de texto preferido

## Instruções de Build

Para compilar o projeto:

```bash
make
```

Isso irá:
1. Criar o diretório `build/` se não existir
2. Compilar `src/main.asm` para `build/main.bin`
3. Copiar o binário para `build/eponaos.img`
4. Ajustar o tamanho da imagem para 1440k (tamanho de disquete)

## Executando com QEMU

Para testar o sistema operacional:

```bash
qemu-system-x86_64 -fda build/eponaos.img
```

## Estrutura do Projeto

```
eponaOS/
├── src/
│   └── main.asm          # Código fonte do bootloader
├── build/
│   ├── main.bin          # Binário compilado
│   └── eponaos.img       # Imagem do disquete
├── Makefile              # Sistema de build
└── README.md             # Este arquivo
```

## Funcionalidades Atuais

- Bootloader básico em assembly x86
- Exibição de mensagem "Hello World!" na tela
- Estrutura preparada para expansão futura

## Troubleshooting

- **Bochs tem se mostrado bastante instável**: Verifique este artigo para algumas dicas de troubleshooting
- **Problemas de compilação**: Certifique-se de que o `nasm` está instalado e no PATH
- **Problemas com QEMU**: Verifique se o `qemu-system-x86_64` está instalado

## Licença

Este projeto é de código aberto e está disponível sob a licença MIT.