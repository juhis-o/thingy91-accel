#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

int modem_configure(void);
void lte_handler(const struct lte_lc_evt *const evt);
int configure_low_power(void);
int initialize_modem(void);