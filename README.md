ix-cool
=======

Simple Daemon to keep intel ix core processor cool using p-state


# Installation

## Dependencies

This programm depends on libconfig (http://www.hyperrealm.com/libconfig/)

## Build

`make install`


# Quick start

Use systemd's sytemctl to control this daemon or create a SysV script.

To reload configuration send SIGHUP signal.

# Configuration

Default configuration is created at first start. Edit /etc/conf.d/ix-cool.conf, it is full documented

## Default configuration

This is the default configuration :


        # Period between two temperature checks
        check_period=2
        
        # Critical temperature limit ( 1000 = 1°C ) above which the CPU max p-state is set to the minimum
        temp_critic=76000
        
        # "High" temperature ( 1000 = 1°C ) above which the CPU max p-state is decreased by dec_high_critic
        temp_high=72000
        
        # Maximum normal temperature ( 1000 = 1°C ) above which the CPU max p-state is decreased by dec_ok_high.
        temp_max_ok=68000
        
        # Minimum normal temperature ( 1000 = 1°C ) under wich the CPU max p-state can be increased, bu not above.
        temp_min_ok=60000
        
        # Minimum p-state pourcentage.
        cpu_min=15
        
        # Maximum p-state pourcentage.
        cpu_max=100
        
        # Minimum CPU-usage to increase max p-state if temperature is under temp_min_ok.
        cpu_inc_seil=80
        
        # Decrement step if Core temperature is between temp_high and temp_critic.
        dec_high_critic=20
        
        # Decrement step if Core temperature is between temp_max_ok and temp_high.
        dec_ok_high=5
        
        # Increment step if Core temperature is under temp_min_ok and CPU-usage is above cpu_inc_seil.
        cpu_inc=10



