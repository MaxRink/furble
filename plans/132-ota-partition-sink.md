# 132 - OTA partition sink adapter

This slice connects the signed MQTT OTA sink contract to a streaming inactive
firmware partition without putting flash APIs into the shared state machine.
`PartitionSink` authenticates the complete manifest before accepting `begin`,
including its declared digest, sizes, rollback counter, and transfer identity.
It then accepts only contiguous chunks, hashes bytes as they are written,
verifies streamed digest equality and the injected trust-store signature, and
closes the staged image before allowing a boot-partition switch. A short write,
range error, truncation, verifier failure, image-end failure or activation
failure aborts the target. Same-range retries use target-backed comparison and
never copy the complete image into RAM.

`PartitionTarget` is the platform-neutral seam. `EspIdfPartitionTarget` and
`ArduinoPartitionTarget` are isolated adapters using the inactive app
partition, `esp_ota_write`, `esp_ota_end` and `esp_ota_set_boot_partition`.
The shared sink and SHA-256 implementation compile on host, simulator and
future Nordic ports without ESP or Arduino headers.

Host coverage injects failures at preflight authentication, begin, short/full write,
end and boot switch;
it also covers bounds, truncation, out-of-order writes, retries, digest and
verifier mismatch, abort cleanup and reboot before activation. Hardware still
needs an interrupted-write power-cut test, real inactive-partition sizing on
all five boards, cryptographic trust-store wiring, and bootloader rollback
confirmation.
