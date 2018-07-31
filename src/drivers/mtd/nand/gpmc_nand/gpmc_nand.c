/**
 * @file
 *
 * @date Jul 30, 2018
 * @author Anton Bondarev
 */
#include <util/log.h>

#include <stdint.h>

#include <drivers/omap_gpmc.h>
#include <drivers/mtd/nand.h>

#include <embox/unit.h>

EMBOX_UNIT_INIT(gpmc_nand_init);

static int gpmc_nand_write(uint32_t cmd, uint32_t addr, uint32_t data) {
	return 0;
}

static int gpmc_nand_read(uint32_t cmd, uint32_t addr, uint32_t *data) {
	return 0;
}

static const struct nand_dev_ops gpmc_nand_dev_ops = {
		.nand_write = gpmc_nand_write,
		.nand_read = gpmc_nand_read
};

int gpmc_nand_init(void) {
	int i;

	log_boot_start();

	for (i = 0; i < GPMC_CS_NUM; i++) {
		uint32_t id;

		//gpmc_reg_write(GPMC_PREFETCH_CONFIG2, 1);

		//gpmc_cs_reg_write(i, GPMC_CS_NAND_ADDRESS, 0x0);
		gpmc_cs_reg_write(i, GPMC_CS_NAND_COMMAND, NAND_CMD_READID);
		//gpmc_cs_reg_write(i, GPMC_CS_NAND_COMMAND, NAND_CMD_READID);
		id = gpmc_cs_reg_read(i, GPMC_CS_NAND_DATA);
		if (id != 0) {
			log_boot("create nand flash on cs %d id 0x%X\n",i , id);
			nand_create("gpmc_nand", &gpmc_nand_dev_ops);
		}
	}

	log_boot_stop();

	return 0;
}
