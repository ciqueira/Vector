# Vector

[English](README.md)

Vector reuni ferramentas para desenvolvimento de look com operações de split tone
e saturação organizadas em um única OFX. O foco é concentrar ajustes
relacionados, manter o sinal RGB de entrada como referência e reduzir a
necessidade de empilhar várias ferramentas pequenas para o mesmo tipo de
correção.

O processamento combina uma etapa em série de split tone com dois ramos
paralelos de saturação. A ferramenta trabalha de forma direta, com controles
separados para saturação por croma, saturação por zona tonal e visualização das
curvas.

Vector é distribuído pelo [MCNexus](https://github.com/ciqueira/MCNexus). O
Nexus fornece distribuição, licenciamento, entrega de atualizações e suporte ao
produto. O MCNexus é o aplicativo desktop usado para ativar, instalar,
atualizar e gerenciar o plugin.

## Plugins Incluídos

| Plugin | Versão | Distribuição | Obter Chave |
| --- | --- | --- | --- |
| Vector | Atual | OpenKey | [Obter Chave](https://bridge.magnociqueira.com.br/github/claim?t=vector-oss&tmpl=363f9ad5-7dec-4e29-86e6-b5923dbfb2d4&sig=ea36f997f27e41cd) |

## Vector

Vector mantém o processamento principal no sinal RGB de entrada. O controle
`Input Space` define o preset usado como referência para pivots e curvas, mas
não transforma o plugin em um conversor completo de espaço de cor.

Presets de entrada disponíveis:

- ACES AP1 / ACEScct
- DaVinci Wide Gamut / Intermediate
- ARRI Wide Gamut 3 / LogC3
- ARRI Wide Gamut 4 / LogC4

O controle `Model/Space Type` define como os ramos de saturação interpretam o
sinal:

- `RGB Direct`: mede a abertura entre canais RGB.
- `RGB Spherical`: usa uma leitura esférica ao redor do eixo neutro.
- `OKLCH`: usa uma leitura perceptual baseada em Oklab.

## Modelo de Processamento

A ordem de processamento começa pelo split tone:

```text
Input RGB -> Split Tone -> Split RGB

Split RGB -> Curves Saturation -> delta de saturação
Split RGB -> Zone Saturation   -> delta de saturação por zona

Split RGB + deltas de saturação -> Output RGB
```

O split tone é serial e cria primeiro a base com separação entre sombras e
altas luzes. Depois disso, Curves Saturation e Zone Saturation rodam em
paralelo a partir da mesma fonte `Split RGB`. Um ramo de saturação não alimenta
o outro; os dois contribuem para o resultado final a partir do mesmo estado da
imagem.

## Controles

`Split Tone` cria separação de cor entre sombras e altas luzes. Os controles
incluem força para sombra e highlight, proteção de preto e branco neutros,
largura e deslocamento de pivot, bias de curva e visualização da curva.

`Curves Saturation` ajusta baixa, média e alta saturação. `Global Sat` altera a
resposta geral, enquanto `Curve Amount` mistura a curva de volta para uma
resposta neutra.

`Zone Saturation` altera saturação por região tonal. O controle `Zone Focus`
direciona a ação para sombras, altas luzes ou ambos os lados do pivot. `Pivot`,
`Pivot Width` e `Sat Strength` definem a transição e a intensidade.

## Suporte de Plataforma

Os builds atuais suportam:

- macOS, Apple Silicon e Macs Intel compatíveis
- Windows x64

Backends de processamento suportados:

- Metal no macOS
- CUDA no Windows

## Instalação

1. Use o link `Obter Chave` acima para gerar a licença OpenKey com uma conta
   GitHub.
2. Abra o MCNexus.
3. Ative o Vector com a chave emitida.
4. Instale ou atualize o plugin pelo MCNexus.

Perda de chave: o mesmo link de solicitação, aberto com a mesma conta GitHub,
recupera a licença já emitida.

## Licença

Vector é *source-available* para revisão, documentação e transparência técnica.
O acesso público a este repositório não torna o projeto software open source.

Consulte:

- [LICENSE.md](LICENSE.md)
- [BINARY_LICENSE.md](BINARY_LICENSE.md)
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## Releases Binários

Os releases binários oficiais são distribuídos pelo Nexus e instalados com o
MCNexus. Use apenas canais oficiais do MCNexus ou do projeto para binários,
atualizações e ativação.
