# Operación

## Entornos soportados

| Componente | Versión de referencia |
| ---------- | --------------------- |
| C++        | C++20                 |
| MSVC       | 19.51 o posterior     |
| GCC        | 11 o posterior        |
| Clang      | 14 o posterior        |
| Node.js    | 24 o posterior        |
| Bun        | 1.3.14                |
| CMake      | 3.20 o posterior      |

## Instalación

```bash
git clone https://github.com/SolguardLabs/BastionDTL.git
cd BastionDTL
bun install --frozen-lockfile
```

No se requieren servicios de red ni base de datos para compilar y ejecutar el motor.

## Compilación portable

```bash
bun run build
```

`scripts/build.mjs` busca, en orden:

1. el compilador indicado en `CXX`;
2. `clang++`, `g++`, `c++` o `cl` según plataforma;
3. instalaciones habituales de Visual Studio mediante `vcvars64.bat`.

El resultado se escribe en `out/bastiondtl` o `out/bastiondtl.exe`.

### Windows

```powershell
bun run build
.\out\bastiondtl.exe --help
```

Si la detección automática no encuentra MSVC, abra una Developer PowerShell y repita el comando.

### CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Configuración

El estado de ejemplo se construye en `make_default_ledger`. Una integración debe sustituir la
configuración por una fuente validada y versionada. Los elementos mínimos son:

- `networkId`;
- activo nativo;
- identidades y roles;
- cuentas y propietarios;
- grants de operador;
- políticas económicas;
- límites de liquidez;
- revisores y quórums.

Nunca acepte una configuración parcial. Valide primero identidades y destinos, luego instale grants
y finalmente habilite la entrada de recibos.

## Comandos

```text
bastiondtl --help
bastiondtl list [--json]
bastiondtl scenario <name> [--json]
```

El modo JSON es el contrato de automatización. El modo texto está orientado a inspección humana y no
debe parsearse desde integraciones.

## Códigos de salida

| Código | Significado                                             |
| -----: | ------------------------------------------------------- |
|    `0` | Escenario ejecutado y controles positivos.              |
|    `1` | Entrada, dominio, transporte o ejecución no válidos.    |
|    `2` | Escenario completado con uno o más controles negativos. |

## Pipeline de operación

```mermaid
flowchart LR
    CONFIG["Configuración versionada"] --> PREFLIGHT["Preflight"]
    PREFLIGHT --> BUILD["Build reproducible"]
    BUILD --> TEST["Typecheck + tests"]
    TEST --> START["Inicio del servicio"]
    START --> HEALTH["Snapshot + reconciliación"]
    HEALTH --> TRAFFIC["Procesamiento"]
    TRAFFIC --> RISK["Control de liquidez"]
    RISK --> ARCHIVE["Archivo de evidencia"]
```

## Preflight

Antes de habilitar procesamiento:

- la versión del binario coincide con la aprobada;
- `networkId` coincide con el entorno;
- todas las identidades esperadas están presentes;
- cada cuenta de custodia tiene propietario y política;
- cuentas de fee y reserva existen con el mismo activo;
- la reconciliación no devuelve observaciones;
- el digest de snapshot coincide entre instancias;
- los límites de liquidez están cargados;
- quórums y pesos de gobierno suman lo previsto;
- el sistema de logs no expone material sensible.

## Salud

Use `snapshot` para el estado contable y `liquidity` para el estado económico:

```bash
out/bastiondtl scenario snapshot --json
out/bastiondtl scenario liquidity --json
```

Señales mínimas:

| Señal                    | Condición                                   |
| ------------------------ | ------------------------------------------- |
| `ok`                     | `true`                                      |
| `reconciliation.ok`      | `true`                                      |
| `stateDigest`            | presente y estable para la misma entrada    |
| `totalSupply`            | coincide con el ledger externo              |
| `liquidity.withinLimits` | `true` para habilitar nuevos lotes          |
| `governance.digest`      | coincide entre observadores del mismo epoch |

## Observabilidad

Registre como campos estructurados:

```text
timestamp
networkId
version
epoch
scenario / command
accountId / receiptId / batchId / changeId
decision
stateDigest
reconciliationDigest
liquidityDigest
governanceDigest
durationMs
```

No registre firmas completas, material de identidad ni payloads que contengan información sensible.
Use IDs y digests para correlación.

## Reconciliación periódica

Una cadencia recomendada:

| Frecuencia     | Control                                    |
| -------------- | ------------------------------------------ |
| Por recibo     | Identidad del reparto y suministro.        |
| Por lote       | Manifiesto, resultados y batch digest.     |
| Cada epoch     | Reconciliación completa y snapshot.        |
| Tras un cambio | Estado de gobierno y subsistema afectado.  |
| Diario         | Cobertura, déficit, HHI y mayores shares.  |
| Por release    | Binario, fuentes, tag y rama `production`. |

## Backups y recuperación

El material operativo que debe conservarse incluye:

- configuraciones canónicas;
- diario ordenado;
- libro de nonces consumidos;
- recibos y manifiestos aceptados;
- propuestas y votos;
- snapshots y digests;
- artefactos de release.

Pruebe restauraciones en un entorno aislado. Una restauración correcta reproduce `stateDigest`,
suministro, conteo de recibos, estados de cuenta y digest de gobierno para el mismo epoch.

## Actualización

1. revise notas y diferencias de esquema;
2. ejecute `bun install --frozen-lockfile`;
3. ejecute `bun run ci`;
4. compare el snapshot antes y después;
5. despliegue primero una instancia sin tráfico;
6. confirme versión, network y digests;
7. habilite tráfico progresivamente;
8. conserve un criterio de rollback verificable.

## Cierre controlado

Durante un cierre:

1. detenga admisión de nuevos lotes;
2. complete operaciones en curso;
3. archive resultados y diario;
4. ejecute reconciliación y liquidez;
5. guarde digests finales;
6. cierre el proceso con timeout acotado;
7. verifique que el proceso sustituto carga el mismo estado.

## SLO sugeridos

| Objetivo                              | Valor inicial |
| ------------------------------------- | ------------: |
| Respuestas CLI válidas                |       99,95 % |
| Reconciliaciones positivas            |         100 % |
| Divergencia de suministro tolerada    |    0 unidades |
| Votos duplicados aceptados            |             0 |
| Cambios ejecutados antes de `readyAt` |             0 |
| Carteras procesadas fuera de límites  |             0 |

Los objetivos deben revisarse con volumen real, latencia de almacenamiento y criticidad de cada
integración.
