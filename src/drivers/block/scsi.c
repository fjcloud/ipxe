/*
 * Copyright (C) 2006 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <byteswap.h>
#include <errno.h>
#include <ipxe/list.h>
#include <ipxe/process.h>
#include <ipxe/xfer.h>
#include <ipxe/blockdev.h>
#include <ipxe/scsi.h>

/** @file
 *
 * SCSI block device
 *
 */

/** Maximum number of TEST UNIT READY retries */
#define SCSI_READY_MAX_RETRIES 10

/* Error numbers generated by SCSI sense data */
#define EIO_NO_SENSE __einfo_error ( EINFO_EIO_NO_SENSE )
#define EINFO_EIO_NO_SENSE \
	__einfo_uniqify ( EINFO_EIO, 0x00, "No sense" )
#define EIO_RECOVERED_ERROR __einfo_error ( EINFO_EIO_RECOVERED_ERROR )
#define EINFO_EIO_RECOVERED_ERROR \
	__einfo_uniqify ( EINFO_EIO, 0x01, "Recovered error" )
#define EIO_NOT_READY __einfo_error ( EINFO_EIO_NOT_READY )
#define EINFO_EIO_NOT_READY \
	__einfo_uniqify ( EINFO_EIO, 0x02, "Not ready" )
#define EIO_MEDIUM_ERROR __einfo_error ( EINFO_EIO_MEDIUM_ERROR )
#define EINFO_EIO_MEDIUM_ERROR \
	__einfo_uniqify ( EINFO_EIO, 0x03, "Medium error" )
#define EIO_HARDWARE_ERROR __einfo_error ( EINFO_EIO_HARDWARE_ERROR )
#define EINFO_EIO_HARDWARE_ERROR \
	__einfo_uniqify ( EINFO_EIO, 0x04, "Hardware error" )
#define EIO_ILLEGAL_REQUEST __einfo_error ( EINFO_EIO_ILLEGAL_REQUEST )
#define EINFO_EIO_ILLEGAL_REQUEST \
	__einfo_uniqify ( EINFO_EIO, 0x05, "Illegal request" )
#define EIO_UNIT_ATTENTION __einfo_error ( EINFO_EIO_UNIT_ATTENTION )
#define EINFO_EIO_UNIT_ATTENTION \
	__einfo_uniqify ( EINFO_EIO, 0x06, "Unit attention" )
#define EIO_DATA_PROTECT __einfo_error ( EINFO_EIO_DATA_PROTECT )
#define EINFO_EIO_DATA_PROTECT \
	__einfo_uniqify ( EINFO_EIO, 0x07, "Data protect" )
#define EIO_BLANK_CHECK __einfo_error ( EINFO_EIO_BLANK_CHECK )
#define EINFO_EIO_BLANK_CHECK \
	__einfo_uniqify ( EINFO_EIO, 0x08, "Blank check" )
#define EIO_VENDOR_SPECIFIC __einfo_error ( EINFO_EIO_VENDOR_SPECIFIC )
#define EINFO_EIO_VENDOR_SPECIFIC \
	__einfo_uniqify ( EINFO_EIO, 0x09, "Vendor specific" )
#define EIO_COPY_ABORTED __einfo_error ( EINFO_EIO_COPY_ABORTED )
#define EINFO_EIO_COPY_ABORTED \
	__einfo_uniqify ( EINFO_EIO, 0x0a, "Copy aborted" )
#define EIO_ABORTED_COMMAND __einfo_error ( EINFO_EIO_ABORTED_COMMAND )
#define EINFO_EIO_ABORTED_COMMAND \
	__einfo_uniqify ( EINFO_EIO, 0x0b, "Aborted command" )
#define EIO_RESERVED __einfo_error ( EINFO_EIO_RESERVED )
#define EINFO_EIO_RESERVED \
	__einfo_uniqify ( EINFO_EIO, 0x0c, "Reserved" )
#define EIO_VOLUME_OVERFLOW __einfo_error ( EINFO_EIO_VOLUME_OVERFLOW )
#define EINFO_EIO_VOLUME_OVERFLOW \
	__einfo_uniqify ( EINFO_EIO, 0x0d, "Volume overflow" )
#define EIO_MISCOMPARE __einfo_error ( EINFO_EIO_MISCOMPARE )
#define EINFO_EIO_MISCOMPARE \
	__einfo_uniqify ( EINFO_EIO, 0x0e, "Miscompare" )
#define EIO_COMPLETED __einfo_error ( EINFO_EIO_COMPLETED )
#define EINFO_EIO_COMPLETED \
	__einfo_uniqify ( EINFO_EIO, 0x0f, "Completed" )
#define EIO_SENSE( key )						\
	EUNIQ ( EINFO_EIO, (key), EIO_NO_SENSE, EIO_RECOVERED_ERROR,	\
		EIO_NOT_READY, EIO_MEDIUM_ERROR, EIO_HARDWARE_ERROR,	\
		EIO_ILLEGAL_REQUEST, EIO_UNIT_ATTENTION,		\
		EIO_DATA_PROTECT, EIO_BLANK_CHECK, EIO_VENDOR_SPECIFIC,	\
		EIO_COPY_ABORTED, EIO_ABORTED_COMMAND, EIO_RESERVED,	\
		EIO_VOLUME_OVERFLOW, EIO_MISCOMPARE, EIO_COMPLETED )

/******************************************************************************
 *
 * Utility functions
 *
 ******************************************************************************
 */

/**
 * Parse SCSI LUN
 *
 * @v lun_string	LUN string representation
 * @v lun		LUN to fill in
 * @ret rc		Return status code
 */
int scsi_parse_lun ( const char *lun_string, struct scsi_lun *lun ) {
	char *p;
	int i;

	memset ( lun, 0, sizeof ( *lun ) );
	if ( lun_string ) {
		p = ( char * ) lun_string;
		for ( i = 0 ; i < 4 ; i++ ) {
			lun->u16[i] = htons ( strtoul ( p, &p, 16 ) );
			if ( *p == '\0' )
				break;
			if ( *p != '-' )
				return -EINVAL;
			p++;
		}
		if ( *p )
			return -EINVAL;
	}

	return 0;
}

/**
 * Parse SCSI sense data
 *
 * @v data		Raw sense data
 * @v len		Length of raw sense data
 * @v sense		Descriptor-format sense data to fill in
 */
void scsi_parse_sense ( const void *data, size_t len,
			struct scsi_sns_descriptor *sense ) {
	const union scsi_sns *sns = data;

	/* Avoid returning uninitialised data */
	memset ( sense, 0, sizeof ( *sense ) );

	/* Copy, assuming descriptor-format data */
	if ( len < sizeof ( sns->desc ) )
		return;
	memcpy ( sense, &sns->desc, sizeof ( *sense ) );

	/* Convert fixed-format to descriptor-format, if applicable */
	if ( len < sizeof ( sns->fixed ) )
		return;
	if ( ! SCSI_SENSE_FIXED ( sns->code ) )
		return;
	sense->additional = sns->fixed.additional;
}

/******************************************************************************
 *
 * Interface methods
 *
 ******************************************************************************
 */

/**
 * Issue SCSI command
 *
 * @v control		SCSI control interface
 * @v data		SCSI data interface
 * @v command		SCSI command
 * @ret tag		Command tag, or negative error
 */
int scsi_command ( struct interface *control, struct interface *data,
		   struct scsi_cmd *command ) {
	struct interface *dest;
	scsi_command_TYPE ( void * ) *op =
		intf_get_dest_op ( control, scsi_command, &dest );
	void *object = intf_object ( dest );
	int tap;

	if ( op ) {
		tap = op ( object, data, command );
	} else {
		/* Default is to fail to issue the command */
		tap = -EOPNOTSUPP;
	}

	intf_put ( dest );
	return tap;
}

/**
 * Report SCSI response
 *
 * @v interface		SCSI command interface
 * @v response		SCSI response
 */
void scsi_response ( struct interface *intf, struct scsi_rsp *response ) {
	struct interface *dest;
	scsi_response_TYPE ( void * ) *op =
		intf_get_dest_op ( intf, scsi_response, &dest );
	void *object = intf_object ( dest );

	if ( op ) {
		op ( object, response );
	} else {
		/* Default is to ignore the response */
	}

	intf_put ( dest );
}

/******************************************************************************
 *
 * SCSI devices and commands
 *
 ******************************************************************************
 */

/** A SCSI device */
struct scsi_device {
	/** Reference count */
	struct refcnt refcnt;
	/** Block control interface */
	struct interface block;
	/** SCSI control interface */
	struct interface scsi;

	/** SCSI LUN */
	struct scsi_lun lun;
	/** Flags */
	unsigned int flags;

	/** TEST UNIT READY interface */
	struct interface ready;
	/** TEST UNIT READY process */
	struct process process;
	/** TEST UNIT READY retry count */
	unsigned int retries;

	/** List of commands */
	struct list_head cmds;
};

/** SCSI device flags */
enum scsi_device_flags {
	/** TEST UNIT READY has been issued */
	SCSIDEV_UNIT_TESTED = 0x0001,
	/** TEST UNIT READY has completed successfully */
	SCSIDEV_UNIT_READY = 0x0002,
};

/** A SCSI command */
struct scsi_command {
	/** Reference count */
	struct refcnt refcnt;
	/** SCSI device */
	struct scsi_device *scsidev;
	/** List of SCSI commands */
	struct list_head list;

	/** Block data interface */
	struct interface block;
	/** SCSI data interface */
	struct interface scsi;

	/** Command type */
	struct scsi_command_type *type;
	/** Starting logical block address */
	uint64_t lba;
	/** Number of blocks */
	unsigned int count;
	/** Data buffer */
	userptr_t buffer;
	/** Length of data buffer */
	size_t len;
	/** Command tag */
	uint32_t tag;

	/** Private data */
	uint8_t priv[0];
};

/** A SCSI command type */
struct scsi_command_type {
	/** Name */
	const char *name;
	/** Additional working space */
	size_t priv_len;
	/**
	 * Construct SCSI command IU
	 *
	 * @v scsicmd		SCSI command
	 * @v command		SCSI command IU
	 */
	void ( * cmd ) ( struct scsi_command *scsicmd,
			 struct scsi_cmd *command );
	/**
	 * Handle SCSI command completion
	 *
	 * @v scsicmd		SCSI command
	 * @v rc		Reason for completion
	 */
	void ( * done ) ( struct scsi_command *scsicmd, int rc );
};

/**
 * Get reference to SCSI device
 *
 * @v scsidev		SCSI device
 * @ret scsidev		SCSI device
 */
static inline __attribute__ (( always_inline )) struct scsi_device *
scsidev_get ( struct scsi_device *scsidev ) {
	ref_get ( &scsidev->refcnt );
	return scsidev;
}

/**
 * Drop reference to SCSI device
 *
 * @v scsidev		SCSI device
 */
static inline __attribute__ (( always_inline )) void
scsidev_put ( struct scsi_device *scsidev ) {
	ref_put ( &scsidev->refcnt );
}

/**
 * Get reference to SCSI command
 *
 * @v scsicmd		SCSI command
 * @ret scsicmd		SCSI command
 */
static inline __attribute__ (( always_inline )) struct scsi_command *
scsicmd_get ( struct scsi_command *scsicmd ) {
	ref_get ( &scsicmd->refcnt );
	return scsicmd;
}

/**
 * Drop reference to SCSI command
 *
 * @v scsicmd		SCSI command
 */
static inline __attribute__ (( always_inline )) void
scsicmd_put ( struct scsi_command *scsicmd ) {
	ref_put ( &scsicmd->refcnt );
}

/**
 * Get SCSI command private data
 *
 * @v scsicmd		SCSI command
 * @ret priv		Private data
 */
static inline __attribute__ (( always_inline )) void *
scsicmd_priv ( struct scsi_command *scsicmd ) {
	return scsicmd->priv;
}

/**
 * Free SCSI command
 *
 * @v refcnt		Reference count
 */
static void scsicmd_free ( struct refcnt *refcnt ) {
	struct scsi_command *scsicmd =
		container_of ( refcnt, struct scsi_command, refcnt );

	/* Drop reference to SCSI device */
	scsidev_put ( scsicmd->scsidev );

	/* Free command */
	free ( scsicmd );
}

/**
 * Close SCSI command
 *
 * @v scsicmd		SCSI command
 * @v rc		Reason for close
 */
static void scsicmd_close ( struct scsi_command *scsicmd, int rc ) {
	struct scsi_device *scsidev = scsicmd->scsidev;

	if ( rc != 0 ) {
		DBGC ( scsidev, "SCSI %p tag %08x closed: %s\n",
		       scsidev, scsicmd->tag, strerror ( rc ) );
	}

	/* Remove from list of commands */
	list_del ( &scsicmd->list );

	/* Shut down interfaces */
	intfs_shutdown ( rc, &scsicmd->scsi, &scsicmd->block, NULL );

	/* Drop list's reference */
	scsicmd_put ( scsicmd );
}

/**
 * Construct and issue SCSI command
 *
 * @ret rc		Return status code
 */
static int scsicmd_command ( struct scsi_command *scsicmd ) {
	struct scsi_device *scsidev = scsicmd->scsidev;
	struct scsi_cmd command;
	int tag;
	int rc;

	/* Construct command */
	memset ( &command, 0, sizeof ( command ) );
	memcpy ( &command.lun, &scsidev->lun, sizeof ( command.lun ) );
	scsicmd->type->cmd ( scsicmd, &command );

	/* Issue command */
	if ( ( tag = scsi_command ( &scsidev->scsi, &scsicmd->scsi,
				    &command ) ) < 0 ) {
		rc = tag;
		DBGC ( scsidev, "SCSI %p could not issue command: %s\n",
		       scsidev, strerror ( rc ) );
		return rc;
	}

	/* Record tag */
	if ( scsicmd->tag ) {
		DBGC ( scsidev, "SCSI %p tag %08x is now tag %08x\n",
		       scsidev, scsicmd->tag, tag );
	}
	scsicmd->tag = tag;
	DBGC2 ( scsidev, "SCSI %p tag %08x %s " SCSI_CDB_FORMAT "\n",
		scsidev, scsicmd->tag, scsicmd->type->name,
		SCSI_CDB_DATA ( command.cdb ) );

	return 0;
}

/**
 * Handle SCSI command completion
 *
 * @v scsicmd		SCSI command
 * @v rc		Reason for close
 */
static void scsicmd_done ( struct scsi_command *scsicmd, int rc ) {

	/* Restart SCSI interface */
	intf_restart ( &scsicmd->scsi, rc );

	/* Hand over to the command completion handler */
	scsicmd->type->done ( scsicmd, rc );
}

/**
 * Handle SCSI response
 *
 * @v scsicmd		SCSI command
 * @v response		SCSI response
 */
static void scsicmd_response ( struct scsi_command *scsicmd,
			       struct scsi_rsp *response ) {
	struct scsi_device *scsidev = scsicmd->scsidev;
	size_t overrun;
	size_t underrun;
	int rc;

	if ( response->status == 0 ) {
		scsicmd_done ( scsicmd, 0 );
	} else {
		DBGC ( scsidev, "SCSI %p tag %08x status %02x",
		       scsidev, scsicmd->tag, response->status );
		if ( response->overrun > 0 ) {
			overrun = response->overrun;
			DBGC ( scsidev, " overrun +%zd", overrun );
		} else if ( response->overrun < 0 ) {
			underrun = -(response->overrun);
			DBGC ( scsidev, " underrun -%zd", underrun );
		}
		DBGC ( scsidev, " sense %02x key %02x additional %04x\n",
		       ( response->sense.code & SCSI_SENSE_CODE_MASK ),
		       ( response->sense.key & SCSI_SENSE_KEY_MASK ),
		       ntohs ( response->sense.additional ) );

		/* Construct error number from sense data */
		rc = -EIO_SENSE ( response->sense.key & SCSI_SENSE_KEY_MASK );
		scsicmd_done ( scsicmd, rc );
	}
}

/**
 * Construct SCSI READ command
 *
 * @v scsicmd		SCSI command
 * @v command		SCSI command IU
 */
static void scsicmd_read_cmd ( struct scsi_command *scsicmd,
			       struct scsi_cmd *command ) {

	if ( ( scsicmd->lba + scsicmd->count ) > SCSI_MAX_BLOCK_10 ) {
		/* Use READ (16) */
		command->cdb.read16.opcode = SCSI_OPCODE_READ_16;
		command->cdb.read16.lba = cpu_to_be64 ( scsicmd->lba );
		command->cdb.read16.len = cpu_to_be32 ( scsicmd->count );
	} else {
		/* Use READ (10) */
		command->cdb.read10.opcode = SCSI_OPCODE_READ_10;
		command->cdb.read10.lba = cpu_to_be32 ( scsicmd->lba );
		command->cdb.read10.len = cpu_to_be16 ( scsicmd->count );
	}
	command->data_in = scsicmd->buffer;
	command->data_in_len = scsicmd->len;
}

/** SCSI READ command type */
static struct scsi_command_type scsicmd_read = {
	.name = "READ",
	.cmd = scsicmd_read_cmd,
	.done = scsicmd_close,
};

/**
 * Construct SCSI WRITE command
 *
 * @v scsicmd		SCSI command
 * @v command		SCSI command IU
 */
static void scsicmd_write_cmd ( struct scsi_command *scsicmd,
				struct scsi_cmd *command ) {

	if ( ( scsicmd->lba + scsicmd->count ) > SCSI_MAX_BLOCK_10 ) {
		/* Use WRITE (16) */
		command->cdb.write16.opcode = SCSI_OPCODE_WRITE_16;
		command->cdb.write16.lba = cpu_to_be64 ( scsicmd->lba );
		command->cdb.write16.len = cpu_to_be32 ( scsicmd->count );
	} else {
		/* Use WRITE (10) */
		command->cdb.write10.opcode = SCSI_OPCODE_WRITE_10;
		command->cdb.write10.lba = cpu_to_be32 ( scsicmd->lba );
		command->cdb.write10.len = cpu_to_be16 ( scsicmd->count );
	}
	command->data_out = scsicmd->buffer;
	command->data_out_len = scsicmd->len;
}

/** SCSI WRITE command type */
static struct scsi_command_type scsicmd_write = {
	.name = "WRITE",
	.cmd = scsicmd_write_cmd,
	.done = scsicmd_close,
};

/** SCSI READ CAPACITY private data */
struct scsi_read_capacity_private {
	/** Use READ CAPACITY (16) */
	int use16;
	/** Data buffer for READ CAPACITY commands */
	union {
		/** Data buffer for READ CAPACITY (10) */
		struct scsi_capacity_10 capacity10;
		/** Data buffer for READ CAPACITY (16) */
		struct scsi_capacity_16 capacity16;
	} capacity;
};

/**
 * Construct SCSI READ CAPACITY command
 *
 * @v scsicmd		SCSI command
 * @v command		SCSI command IU
 */
static void scsicmd_read_capacity_cmd ( struct scsi_command *scsicmd,
					struct scsi_cmd *command ) {
	struct scsi_read_capacity_private *priv = scsicmd_priv ( scsicmd );
	struct scsi_cdb_read_capacity_16 *readcap16 = &command->cdb.readcap16;
	struct scsi_cdb_read_capacity_10 *readcap10 = &command->cdb.readcap10;
	struct scsi_capacity_16 *capacity16 = &priv->capacity.capacity16;
	struct scsi_capacity_10 *capacity10 = &priv->capacity.capacity10;

	if ( priv->use16 ) {
		/* Use READ CAPACITY (16) */
		readcap16->opcode = SCSI_OPCODE_SERVICE_ACTION_IN;
		readcap16->service_action =
			SCSI_SERVICE_ACTION_READ_CAPACITY_16;
		readcap16->len = cpu_to_be32 ( sizeof ( *capacity16 ) );
		command->data_in = virt_to_user ( capacity16 );
		command->data_in_len = sizeof ( *capacity16 );
	} else {
		/* Use READ CAPACITY (10) */
		readcap10->opcode = SCSI_OPCODE_READ_CAPACITY_10;
		command->data_in = virt_to_user ( capacity10 );
		command->data_in_len = sizeof ( *capacity10 );
	}
}

/**
 * Handle SCSI READ CAPACITY command completion
 *
 * @v scsicmd		SCSI command
 * @v rc		Reason for completion
 */
static void scsicmd_read_capacity_done ( struct scsi_command *scsicmd,
					 int rc ) {
	struct scsi_device *scsidev = scsicmd->scsidev;
	struct scsi_read_capacity_private *priv = scsicmd_priv ( scsicmd );
	struct scsi_capacity_16 *capacity16 = &priv->capacity.capacity16;
	struct scsi_capacity_10 *capacity10 = &priv->capacity.capacity10;
	struct block_device_capacity capacity;

	/* Close if command failed */
	if ( rc != 0 ) {
		scsicmd_close ( scsicmd, rc );
		return;
	}

	/* Extract capacity */
	if ( priv->use16 ) {
		capacity.blocks = ( be64_to_cpu ( capacity16->lba ) + 1 );
		capacity.blksize = be32_to_cpu ( capacity16->blksize );
	} else {
		capacity.blocks = ( be32_to_cpu ( capacity10->lba ) + 1 );
		capacity.blksize = be32_to_cpu ( capacity10->blksize );

		/* If capacity range was exceeded (i.e. capacity.lba
		 * was 0xffffffff, meaning that blockdev->blocks is
		 * now zero), use READ CAPACITY (16) instead.  READ
		 * CAPACITY (16) is not mandatory, so we can't just
		 * use it straight off.
		 */
		if ( capacity.blocks == 0 ) {
			priv->use16 = 1;
			if ( ( rc = scsicmd_command ( scsicmd ) ) != 0 ) {
				scsicmd_close ( scsicmd, rc );
				return;
			}
			return;
		}
	}
	capacity.max_count = -1U;

	/* Allow transport layer to update capacity */
	block_capacity ( &scsidev->scsi, &capacity );

	/* Return capacity to caller */
	block_capacity ( &scsicmd->block, &capacity );

	/* Close command */
	scsicmd_close ( scsicmd, 0 );
}

/** SCSI READ CAPACITY command type */
static struct scsi_command_type scsicmd_read_capacity = {
	.name = "READ CAPACITY",
	.priv_len = sizeof ( struct scsi_read_capacity_private ),
	.cmd = scsicmd_read_capacity_cmd,
	.done = scsicmd_read_capacity_done,
};

/**
 * Construct SCSI TEST UNIT READY command
 *
 * @v scsicmd		SCSI command
 * @v command		SCSI command IU
 */
static void scsicmd_test_unit_ready_cmd ( struct scsi_command *scsicmd __unused,
					  struct scsi_cmd *command ) {
	struct scsi_cdb_test_unit_ready *testready = &command->cdb.testready;

	testready->opcode = SCSI_OPCODE_TEST_UNIT_READY;
}

/** SCSI TEST UNIT READY command type */
static struct scsi_command_type scsicmd_test_unit_ready = {
	.name = "TEST UNIT READY",
	.cmd = scsicmd_test_unit_ready_cmd,
	.done = scsicmd_close,
};

/** SCSI command block interface operations */
static struct interface_operation scsicmd_block_op[] = {
	INTF_OP ( intf_close, struct scsi_command *, scsicmd_close ),
};

/** SCSI command block interface descriptor */
static struct interface_descriptor scsicmd_block_desc =
	INTF_DESC_PASSTHRU ( struct scsi_command, block,
			     scsicmd_block_op, scsi );

/** SCSI command SCSI interface operations */
static struct interface_operation scsicmd_scsi_op[] = {
	INTF_OP ( intf_close, struct scsi_command *, scsicmd_done ),
	INTF_OP ( scsi_response, struct scsi_command *, scsicmd_response ),
};

/** SCSI command SCSI interface descriptor */
static struct interface_descriptor scsicmd_scsi_desc =
	INTF_DESC_PASSTHRU ( struct scsi_command, scsi,
			     scsicmd_scsi_op, block );

/**
 * Create SCSI command
 *
 * @v scsidev		SCSI device
 * @v block		Block data interface
 * @v type		SCSI command type
 * @v lba		Starting logical block address
 * @v count		Number of blocks to transfer
 * @v buffer		Data buffer
 * @v len		Length of data buffer
 * @ret rc		Return status code
 */
static int scsidev_command ( struct scsi_device *scsidev,
			     struct interface *block,
			     struct scsi_command_type *type,
			     uint64_t lba, unsigned int count,
			     userptr_t buffer, size_t len ) {
	struct scsi_command *scsicmd;
	int rc;

	/* Allocate and initialise structure */
	scsicmd = zalloc ( sizeof ( *scsicmd ) + type->priv_len );
	if ( ! scsicmd ) {
		rc = -ENOMEM;
		goto err_zalloc;
	}
	ref_init ( &scsicmd->refcnt, scsicmd_free );
	intf_init ( &scsicmd->block, &scsicmd_block_desc, &scsicmd->refcnt );
	intf_init ( &scsicmd->scsi, &scsicmd_scsi_desc,
		    &scsicmd->refcnt );
	scsicmd->scsidev = scsidev_get ( scsidev );
	list_add ( &scsicmd->list, &scsidev->cmds );
	scsicmd->type = type;
	scsicmd->lba = lba;
	scsicmd->count = count;
	scsicmd->buffer = buffer;
	scsicmd->len = len;

	/* Issue SCSI command */
	if ( ( rc = scsicmd_command ( scsicmd ) ) != 0 )
		goto err_command;

	/* Attach to parent interface, transfer reference to list, and return */
	intf_plug_plug ( &scsicmd->block, block );
	return 0;

 err_command:
	scsicmd_close ( scsicmd, rc );
	ref_put ( &scsicmd->refcnt );
 err_zalloc:
	return rc;
}

/**
 * Issue SCSI block read
 *
 * @v scsidev		SCSI device
 * @v block		Block data interface
 * @v lba		Starting logical block address
 * @v count		Number of blocks to transfer
 * @v buffer		Data buffer
 * @v len		Length of data buffer
 * @ret rc		Return status code

 */
static int scsidev_read ( struct scsi_device *scsidev,
			  struct interface *block,
			  uint64_t lba, unsigned int count,
			  userptr_t buffer, size_t len ) {
	return scsidev_command ( scsidev, block, &scsicmd_read,
				 lba, count, buffer, len );
}

/**
 * Issue SCSI block write
 *
 * @v scsidev		SCSI device
 * @v block		Block data interface
 * @v lba		Starting logical block address
 * @v count		Number of blocks to transfer
 * @v buffer		Data buffer
 * @v len		Length of data buffer
 * @ret rc		Return status code
 */
static int scsidev_write ( struct scsi_device *scsidev,
			   struct interface *block,
			   uint64_t lba, unsigned int count,
			   userptr_t buffer, size_t len ) {
	return scsidev_command ( scsidev, block, &scsicmd_write,
				 lba, count, buffer, len );
}

/**
 * Read SCSI device capacity
 *
 * @v scsidev		SCSI device
 * @v block		Block data interface
 * @ret rc		Return status code
 */
static int scsidev_read_capacity ( struct scsi_device *scsidev,
				   struct interface *block ) {
	return scsidev_command ( scsidev, block, &scsicmd_read_capacity,
				 0, 0, UNULL, 0 );
}

/**
 * Test to see if SCSI device is ready
 *
 * @v scsidev		SCSI device
 * @v block		Block data interface
 * @ret rc		Return status code
 */
static int scsidev_test_unit_ready ( struct scsi_device *scsidev,
				     struct interface *block ) {
	return scsidev_command ( scsidev, block, &scsicmd_test_unit_ready,
				 0, 0, UNULL, 0 );
}

/**
 * Check SCSI device flow-control window
 *
 * @v scsidev		SCSI device
 * @ret len		Length of window
 */
static size_t scsidev_window ( struct scsi_device *scsidev ) {

	/* Refuse commands until unit is confirmed ready */
	if ( ! ( scsidev->flags & SCSIDEV_UNIT_READY ) )
		return 0;

	return xfer_window ( &scsidev->scsi );
}

/**
 * Close SCSI device
 *
 * @v scsidev		SCSI device
 * @v rc		Reason for close
 */
static void scsidev_close ( struct scsi_device *scsidev, int rc ) {
	struct scsi_command *scsicmd;
	struct scsi_command *tmp;

	/* Stop process */
	process_del ( &scsidev->process );

	/* Shut down interfaces */
	intfs_shutdown ( rc, &scsidev->block, &scsidev->scsi, &scsidev->ready,
			 NULL );

	/* Shut down any remaining commands */
	list_for_each_entry_safe ( scsicmd, tmp, &scsidev->cmds, list )
		scsicmd_close ( scsicmd, rc );
}

/** SCSI device block interface operations */
static struct interface_operation scsidev_block_op[] = {
	INTF_OP ( xfer_window, struct scsi_device *, scsidev_window ),
	INTF_OP ( block_read, struct scsi_device *, scsidev_read ),
	INTF_OP ( block_write, struct scsi_device *, scsidev_write ),
	INTF_OP ( block_read_capacity, struct scsi_device *,
		  scsidev_read_capacity ),
	INTF_OP ( intf_close, struct scsi_device *, scsidev_close ),
};

/** SCSI device block interface descriptor */
static struct interface_descriptor scsidev_block_desc =
	INTF_DESC_PASSTHRU ( struct scsi_device, block,
			     scsidev_block_op, scsi );

/**
 * Handle SCSI TEST UNIT READY response
 *
 * @v scsidev		SCSI device
 * @v rc		Reason for close
 */
static void scsidev_ready ( struct scsi_device *scsidev, int rc ) {

	/* Shut down interface */
	intf_restart ( &scsidev->ready, rc );

	/* Mark device as ready, if applicable */
	if ( rc == 0 ) {
		DBGC ( scsidev, "SCSI %p unit is ready\n", scsidev );
		scsidev->flags |= SCSIDEV_UNIT_READY;
		xfer_window_changed ( &scsidev->block );
		return;
	}
	DBGC ( scsidev, "SCSI %p not ready: %s\n", scsidev, strerror ( rc ) );

	/* SCSI targets have an annoying habit of returning occasional
	 * pointless "error" messages such as "power-on occurred", so
	 * we have to be prepared to retry commands.
	 *
	 * For most commands, we rely on the caller (e.g. the generic
	 * SAN device layer) to retry commands as needed.  However, a
	 * TEST UNIT READY failure is used as an indication that the
	 * whole SCSI device is unavailable and should be closed.  We
	 * must therefore perform this retry loop within the SCSI
	 * layer.
	 */
	if ( scsidev->retries++ < SCSI_READY_MAX_RETRIES ) {
		DBGC ( scsidev, "SCSI %p retrying (retry %d)\n",
		       scsidev, scsidev->retries );
		scsidev->flags &= ~SCSIDEV_UNIT_TESTED;
		process_add ( &scsidev->process );
		return;
	}

	/* Close device */
	DBGC ( scsidev, "SCSI %p never became ready: %s\n",
	       scsidev, strerror ( rc ) );
	scsidev_close ( scsidev, rc );
}

/** SCSI device TEST UNIT READY interface operations */
static struct interface_operation scsidev_ready_op[] = {
	INTF_OP ( intf_close, struct scsi_device *, scsidev_ready ),
};

/** SCSI device TEST UNIT READY interface descriptor */
static struct interface_descriptor scsidev_ready_desc =
	INTF_DESC ( struct scsi_device, ready, scsidev_ready_op );

/**
 * SCSI TEST UNIT READY process
 *
 * @v scsidev		SCSI device
 */
static void scsidev_step ( struct scsi_device *scsidev ) {
	int rc;

	/* Do nothing if we have already issued TEST UNIT READY */
	if ( scsidev->flags & SCSIDEV_UNIT_TESTED )
		return;

	/* Wait until underlying SCSI device is ready */
	if ( xfer_window ( &scsidev->scsi ) == 0 )
		return;

	DBGC ( scsidev, "SCSI %p waiting for unit to become ready\n",
	       scsidev );

	/* Mark TEST UNIT READY as sent */
	scsidev->flags |= SCSIDEV_UNIT_TESTED;

	/* Issue TEST UNIT READY command */
	if ( ( rc = scsidev_test_unit_ready ( scsidev, &scsidev->ready )) !=0){
		scsidev_close ( scsidev, rc );
		return;
	}
}

/** SCSI device SCSI interface operations */
static struct interface_operation scsidev_scsi_op[] = {
	INTF_OP ( xfer_window_changed, struct scsi_device *, scsidev_step ),
	INTF_OP ( intf_close, struct scsi_device *, scsidev_close ),
};

/** SCSI device SCSI interface descriptor */
static struct interface_descriptor scsidev_scsi_desc =
	INTF_DESC_PASSTHRU ( struct scsi_device, scsi,
			     scsidev_scsi_op, block );

/** SCSI device process descriptor */
static struct process_descriptor scsidev_process_desc =
	PROC_DESC_ONCE ( struct scsi_device, process, scsidev_step );

/**
 * Open SCSI device
 *
 * @v block		Block control interface
 * @v scsi		SCSI control interface
 * @v lun		SCSI LUN
 * @ret rc		Return status code
 */
int scsi_open ( struct interface *block, struct interface *scsi,
		struct scsi_lun *lun ) {
	struct scsi_device *scsidev;

	/* Allocate and initialise structure */
	scsidev = zalloc ( sizeof ( *scsidev ) );
	if ( ! scsidev )
		return -ENOMEM;
	ref_init ( &scsidev->refcnt, NULL );
	intf_init ( &scsidev->block, &scsidev_block_desc, &scsidev->refcnt );
	intf_init ( &scsidev->scsi, &scsidev_scsi_desc, &scsidev->refcnt );
	intf_init ( &scsidev->ready, &scsidev_ready_desc, &scsidev->refcnt );
	process_init ( &scsidev->process, &scsidev_process_desc,
		       &scsidev->refcnt );
	INIT_LIST_HEAD ( &scsidev->cmds );
	memcpy ( &scsidev->lun, lun, sizeof ( scsidev->lun ) );
	DBGC ( scsidev, "SCSI %p created for LUN " SCSI_LUN_FORMAT "\n",
	       scsidev, SCSI_LUN_DATA ( scsidev->lun ) );

	/* Attach to SCSI and parent interfaces, mortalise self, and return */
	intf_plug_plug ( &scsidev->scsi, scsi );
	intf_plug_plug ( &scsidev->block, block );
	ref_put ( &scsidev->refcnt );
	return 0;
}
