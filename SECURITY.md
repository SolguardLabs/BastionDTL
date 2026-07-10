# Security Model

BastionDTL modela un sistema de custodia DTL con separacion explicita entre
identidades, cuentas, operadores, politicas economicas, recibos firmados y
reconciliacion de ledger.

## Actores

- `Owner`: propietario de una cuenta de custodia.
- `Treasurer`: operador administrativo para fondeo, sweeping autorizado y
  mantenimiento.
- `Operator`: emite recibos de liquidacion para una cuenta asignada.
- `Beneficiary`: recibe pagos liquidados.
- `Auditor`: consume snapshots y digests de estado.

## Invariantes Esperadas

- Cada cuenta mantiene balance disponible, reservado y bloqueado por activo.
- Las cuentas cerradas no deben retener balance.
- Una cuenta abierta debe tener una unica politica operativa corriente.
- Los recibos deben estar firmados por un operador registrado.
- Los nonces de recibo son de un solo uso por cuenta origen.
- La liquidacion no debe crear ni destruir suministro interno.
- Las cuentas de fee y reserva deben existir y compartir activo con la cuenta
  origen.
- Las retiradas respetan el modo de retirada de la politica corriente.

## Validaciones Automatizadas

Los tests TypeScript cubren:

- contrato CLI y JSON;
- liquidacion de recibos firmados;
- rechazo de operador no autorizado;
- bloqueo de retirada por cuenta congelada;
- retirada de propietario;
- rotacion operativa y procesamiento de handoff;
- cierre de cuenta vacia;
- reconciliacion de cuentas, politicas y suministro.

## Dependencias

El core C++ no usa dependencias externas. El tooling de tests usa Bun y Node.js.
Dependabot revisa dependencias npm y GitHub Actions.

## Alcance De Revision

El repositorio es un laboratorio autocontenido. Las firmas deterministas no son
primitivas criptograficas productivas; existen para hacer los escenarios
reproducibles y auditables sin servicios externos.

Los reportes internos deben incluir:

- escenario afectado;
- recibo, cuenta y politica implicados;
- estado antes y despues;
- impacto contable;
- recomendacion de test de regresion.
