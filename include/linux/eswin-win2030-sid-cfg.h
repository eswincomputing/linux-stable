#ifndef ESWIN_WIN2030_SID_CFG_H
#define ESWIN_WIN2030_SID_CFG_H

int win2030_dynm_sid_enable(int nid);
int win2030_aon_sid_cfg(struct device *dev);
int win2030_tbu_power(struct device *dev, bool is_powerUp);
int win2030_tbu_power_by_dev_and_node(struct device *dev, struct device_node *node, bool is_powerUp);

void trigger_waveform_start(void);
void trigger_waveform_stop(void);
void print_tcu_node_status(const char *call_name, int call_line);
#endif
