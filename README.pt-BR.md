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

Vector é distribuído pelo [MCNexus](https://mcnexus.app). O
Nexus fornece distribuição, licenciamento, entrega de atualizações e suporte ao
produto. O MCNexus é o aplicativo desktop usado para ativar, instalar,
atualizar e gerenciar o plugin.

## Plugins Incluídos

| Plugin | Versão | Distribuição | Chave Gratuita | Apoiar o Projeto |
| --- | --- | --- | --- | --- |
| Vector | Atual | OpenKey | [Obter Chave](https://bridge.mcnexus.app/github/claim?t=vector-oss&tmpl=363f9ad5-7dec-4e29-86e6-b5923dbfb2d4&sig=ea36f997f27e41cd) | [Torne-se um Apoiador](https://bridge.mcnexus.app/commerce/start?t=vector-oss&offer=vector-supporter) |

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

## Apoie o Projeto

O Vector continua disponível gratuitamente com todos os recursos publicados
atualmente. Se ele for útil no seu trabalho, você pode apoiar opcionalmente sua
manutenção e evolução.

O benefício Vector Supporter inclui:

- suporte prioritário e privado por e-mail por 12 meses; e
- comunicações operacionais sobre releases, compatibilidade, manutenção,
  segurança e alterações materiais do serviço do Vector.

A compra não adiciona recursos exclusivos ao plugin. Para entregar e associar
o benefício Supporter, o Nexus poderá criar uma nova licença técnica ou
associar e atualizar uma licença existente. Não é necessário obter a chave
gratuita antes do checkout; usuários existentes devem usar a mesma conta
GitHub e o mesmo e-mail verificado.

[Comprar Vector Supporter](https://bridge.mcnexus.app/commerce/start?t=vector-oss&offer=vector-supporter)

Antes da compra, consulte os
[Termos de Supporter](https://legal.magnociqueira.com.br/pt-BR/products/vector/terms/),
a [Política de Reembolso](https://legal.magnociqueira.com.br/pt-BR/products/vector/refunds/),
a [Política de Privacidade](https://legal.magnociqueira.com.br/pt-BR/products/vector/privacy/)
e a [Política de Suporte](https://legal.magnociqueira.com.br/pt-BR/products/vector/support/).
Cópias Markdown permanecem em [`legal/`](legal/pt-BR/README.md) para facilitar
a consulta no repositório. Mensagens sobre produtos não relacionados não são
incluídas automaticamente e exigem uma escolha separada de marketing se ela
for oferecida no futuro.

## Licença

Vector é *source-available* para revisão, documentação e transparência técnica.
O acesso público a este repositório não torna o projeto software open source.

Consulte:

- [LICENSE.md](LICENSE.md)
- [BINARY_LICENSE.md](BINARY_LICENSE.md)
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- [Documentos legais do produto](legal/pt-BR/README.md)

## Releases Binários

Os releases binários oficiais são distribuídos pelo Nexus e instalados com o
MCNexus. Use apenas canais oficiais do MCNexus ou do projeto para binários,
atualizações e ativação.
