/**********************************************************************
** This program is part of 'MOOSE', the
** Messaging Object Oriented Simulation Environment,
** also known as GENESIS 3 base code.
**           copyright (C) 2003-2005 Upinder S. Bhalla. and NCBS
** It is made available under the terms of the
** GNU Lesser General Public License version 2.1
** See the file COPYING.LIB for the full notice.
**********************************************************************/

/**
 * The ClockJob manages simulation scheduling, in a close
 * collaboration with the ClockTick.
 *
 * Simulation scheduling requires that certain functions of 
 * groups of objects be called in a strict sequence according to
 * a range of timesteps, dt. For example, the numerical integration
 * function of a compartment may be called every 10 microseconds.
 * Furthermore, there may be ion channels in the simulation which also
 * have to be called every 10 microseconds, but always after all
 * the compartments have been called. Finally,
 * the graphical update function to plot the compartment voltage
 * may be called every 1 millisecond of simulation time.
 * The whole sequence has to be repeated till the runtime of the 
 * simulation is complete.
 *
 * This scheduler is quite general and handles any combination of
 * simulation times, including non-multiple ratios of dt.
 *
 * The ClockJob part of the team manages a set of ClockTicks.
 * Each ClockTick handles a given dt and a given stage within a dt.
 * The scheduler guarantees that the call sequence is preserved
 * between ClockTicks, but there are no sequence assumptions within
 * a single ClockTick.
 *
 * The system works like this:
 * 1. We create a bunch of ClockTicks on the ClockJob.
 * 2. We assign their dts and stages within each dt, if needed.
 * 3. We connect up the ClockTicks to their target objects.
 * 4. We call Resched on the ClockJob. 
 *    4.1 This strips off any pre-existing 'next' and 'prev' messages
 *    between the ticks (in case they had already been set up)
 *    4.2 It sorts the ClockTicks according to their dt and stage
 *    4.3 It sets up 'next' and 'prev' messages between them according
 *    to this sorted sequence.
 *    4.4 It sets up a 'next' and 'prev' message between itself and
 *    the first tick.
 *    4.5 On its local 'next' message to the first tick, it
 *    calls resched which trickles through all the ticks
 *    setting up state variables
 *    for their sequencing operations.
 * At this point we are all set up.
 * 5. We begin the simulation by calling 'start' or 'step' on the
 * ClockJob.
 *    5.1. Either of these functions work out the runTime and use it
 *    to call 'start' on the local 'next' message to the first tick.
 *    5.2 The start function on the first tick is the main loop for
 *    advancing time. It does the following:
 *       5.2.1 Sets up state variables and the dt in the ProcInfo
 *       Sets up endTime to either the end of the runtime (which
 *       is fine if there are no other ticks) or to the time that
 *       the next clock is due to fire, at nextClockTime.
 *       5.2.2 Loops till the current tick passes endTime, sending
 *       out process calls to all targets of current tick.
 *       5.2.3 Checks if there are any other ticks. If so (as usually
 *       the case), sends out the 'next' message to them with the
 *       time that they have to advance past.
 *           5.2.3.1. This 'next' message invokes the 'incrementClock'
 *           function on the next tick.
 *           5.2.3.2. In the incrementClock function, the next tick
 *           first sends out process calls.
 *           5.2.3.3. If there are any further ticks, it calls its
 *           own 'next' message to the further incrementClock function.
 *           5.2.3.4 Finally, it calls back to the originating 
 *           object telling it what its new value of nextTime_ is.
 *       5.2.4. Repeats this 'next' message till info comes back
 *       from the next tick to update the nextClockTime past the
 *       nextTime_ value.
 *    5.3 When the start function finally returns, the simulation is
 *    done till the desired time. The ClockJob updates its currentTime_
 *    field and returns.
 *
 * 6. We may wish to interrupt the simulation at some intermediate time
 * In principle this should be easy to do by putting in a 'halt'
 * flag. But it isn't implemented yet.
 * 7. We may wish to restart the simulation from where it left off.
 * This is done using the same 'start' or 'step' function on the
 * ClockJob. As all the ticks retain their state, the simulation can
 * resume smoothly.
 */

#include "moose.h"
#include "ClockJob.h"

const Cinfo* initClockJobCinfo()
{

	/**
	 * This sets up the Process shared message. First entry is for
	 * Process, second for Reinit.
	 */
	static TypeFuncPair processTypes[] = {
		TypeFuncPair( Ftype1< ProcInfo >::global(), 0 ),	// Process
		TypeFuncPair( Ftype1< ProcInfo >::global(), 0 ),	// Reinit
		TypeFuncPair( Ftype0::global(), 0 ),				// Resched
		TypeFuncPair( Ftype1< unsigned int >::global(), 0 ), // NewObj
		TypeFuncPair( Ftype1< double >::global(), 
						RFCAST( &ClockJob::dtFunc ) ),	// dtIn
	};

	static Finfo* clockFinfos[] =
	{
	///////////////////////////////////////////////////////
	// Field definitions
	///////////////////////////////////////////////////////
		new ValueFinfo( "runTime", ValueFtype1< double >::global(),
			GFCAST( &ClockJob::getRunTime ), 
			RFCAST( &ClockJob::setRunTime )
			),
		new ValueFinfo( "currentTime", ValueFtype1< double >::global(),
			GFCAST( &ClockJob::getCurrentTime ), 
			&dummyFunc
			),
		new ValueFinfo( "nsteps", ValueFtype1< int >::global(),
			GFCAST( &ClockJob::getNsteps ), 
			RFCAST( &ClockJob::setNsteps )
			),
		new ValueFinfo( "currentStep", ValueFtype1< int >::global(),
			GFCAST( &ClockJob::getCurrentStep ), 
			&dummyFunc
			),
	///////////////////////////////////////////////////////
	// Shared definitions
	///////////////////////////////////////////////////////
		new SharedFinfo( "process", processTypes, 5 ),
		// "processSrc, reinitSrc, reschedSrc, schedNewObjectSrc, dtIn"
	///////////////////////////////////////////////////////
	// MsgSrc definitions
	///////////////////////////////////////////////////////
		new SrcFinfo( "finishedSrc", Ftype0::global() ),
		new SrcFinfo( "start", Ftype2< ProcInfo, double >::global() ),

	///////////////////////////////////////////////////////
	// MsgDest definitions
	///////////////////////////////////////////////////////
		/**
		 * This function sets off the simulation for the specificed
		 * duration
		 */
		new DestFinfo( "start", Ftype1< double >::global(),
					RFCAST( &ClockJob::startFunc ) ),
		/// This sets of the simulation for the specified # of steps
		new DestFinfo( "step", Ftype1< int >::global(),
					RFCAST( &ClockJob::stepFunc ) ),
		new DestFinfo( "reinit", Ftype0::global(),
					RFCAST( &ClockJob::reinitFunc ) ),
		new DestFinfo( "resched", Ftype0::global(),
					RFCAST( &ClockJob::reschedFunc ) ),

	///////////////////////////////////////////////////////
	// Synapse definitions
	///////////////////////////////////////////////////////
	};
	
	static Cinfo clockJobCinfo(
		"ClockJob",
		"Upinder S. Bhalla, Mar 2007, NCBS",
		"ClockJob: ClockJob class. Handles sequencing of operations in simulations",
		initNeutralCinfo(),
		clockFinfos,
		sizeof(clockFinfos)/sizeof(Finfo *),
		ValueFtype1< ClockJob >::global()
	);

	return &clockJobCinfo;
}

static const Cinfo* clockJobCinfo = initClockJobCinfo();

static const unsigned int startSlot = 
	initClockJobCinfo()->getSlotIndex( "start" );
static const unsigned int processSlot = 
	initClockJobCinfo()->getSlotIndex( "process" ) + 0;
static const unsigned int reinitSlot = 
	initClockJobCinfo()->getSlotIndex( "reinit" ) + 1;
static const unsigned int reschedSlot = 
	initClockJobCinfo()->getSlotIndex( "resched" ) + 2;

///////////////////////////////////////////////////
// Field function definitions
///////////////////////////////////////////////////

/**
 * This sets the runtime to use for the simulation.
 * Perhaps this should not be assignable, but readonly.
 * Or The assignment should be a message.
 */
void ClockJob::setRunTime( const Conn& c, double v )
{
	static_cast< ClockJob* >( c.data() )->runTime_ = v;
}
double ClockJob::getRunTime( const Element* e )
{
	return static_cast< ClockJob* >( e->data() )->runTime_;
}

double ClockJob::getCurrentTime( const Element* e )
{
	return static_cast< ClockJob* >( e->data() )->currentTime_;
}

/**
 * This sets the number of steps to use for the simulation.
 * Perhaps this should not be assignable, but readonly.
 * Or The assignment should be a message.
 * It is a variant of the runtime function.
 */
void ClockJob::setNsteps( const Conn& c, int v )
{
	static_cast< ClockJob* >( c.data() )->nSteps_ = v;
}
int ClockJob::getNsteps( const Element* e )
{
	return static_cast< ClockJob* >( e->data() )->nSteps_;
}

int ClockJob::getCurrentStep( const Element* e )
{
	return static_cast< ClockJob* >( e->data() )->currentStep_;
}

///////////////////////////////////////////////////
// Dest function definitions
///////////////////////////////////////////////////


void ClockJob::startFunc( const Conn& c, double runtime)
{
	static_cast< ClockJob* >( c.data() )->startFuncLocal( 
					c.targetElement(), runtime );
}

void ClockJob::startFuncLocal( Element* e, double runTime )
{
	cout << "starting run for " << runTime << " sec.\n";
	send2< ProcInfo, double >( e, startSlot, info_, runTime );
	/*
	info_->currTime_ = currentTime_;
	if ( tick_ )
		tick_->start( info_, currentTime_ + runTime,
			processSrc_ );
	currentTime_ = info_->currTime_;
	*/
}

void ClockJob::stepFunc( const Conn& c, int nsteps )
{
	ClockJob* cj = static_cast< ClockJob* >( c.data() );
	cj->startFuncLocal( c.targetElement(), nsteps * cj->dt_ );
}

/**
 * This function resets the schedule to zero time. It does NOT
 * reorder any of the clock ticks, it assumes that they are scheduled
 * correctly
 */
void ClockJob::reinitFunc( const Conn& c, double runtime)
{
	static_cast< ClockJob* >( c.data() )->reinitFuncLocal(
					c.targetElement() );
}
void ClockJob::reinitFuncLocal( Element* e )
{
	currentTime_ = 0.0;
	currentStep_ = 0;
	send1< ProcInfo >( e, reinitSlot, info_ );
}

/**
 * The resched function reorders all clock ticks according to their
 * current dts. This involves clearing out their internal next and
 * prev messages (which effectively set them up in a linked list),
 * and then rebuilding this list according to the new sorted order.
 * This function does NOT mess with the current simulation time: you
 * can resched an ongoing simulation.
 */
void ClockJob::reschedFunc( const Conn& c )
{
	static_cast< ClockJob* >( c.data() )->reschedFuncLocal(
					c.targetElement() );
}
void ClockJob::reschedFuncLocal( Element* e )
{
		/*
			if ( tick_ )
				delete ( tick_ );
			tick_ = 0;
			vector< Field > f;
			Field kids = field( "processOut" );
			kids->dest( f, this );
			vector< ClockTickMsgSrc > ticks;
			for ( unsigned long i = 0; i < f.size(); i++ )
	 			ticks.push_back( 
					ClockTickMsgSrc( this, f[i].getElement() , i )
				);
			sort( ticks.begin(), ticks.end() );
			vector< ClockTickMsgSrc >::iterator j;
			ClockTickMsgSrc** currTick = &tick_;
			ClockTickMsgSrc* prev = tick_;
			for ( j = ticks.begin(); j != ticks.end(); j++ ) {
				*currTick = new ClockTickMsgSrc( *j );
				prev = *currTick;
				currTick = (*currTick)->next();
			}
			reschedSrc_.send();
			*/
}

/**
 * This function handles any changes to dt in the ticks. This means
 * it must redo the ordering of the ticks and call a resched on them.
 * \todo Currently only a placeholder.
 */
void ClockJob::dtFunc( const Conn& c, double dt )
{
	static_cast< ClockJob* >( c.data() )->dtFuncLocal(
					c.targetElement(), dt );
}
void ClockJob::dtFuncLocal( Element* e, double dt )
{
		/*
	if ( tick_ == 0 )
		return;
	unsigned long index = clockConn_.find( tick ); 
	tick_->updateDt( dt, index ); 
	sortTicks(); 
	*/
}

///////////////////////////////////////////////////
// Other function definitions
///////////////////////////////////////////////////

/*
void ClockJob::sortTicks( )
{
	ClockTickMsgSrc** i;
	ClockTickMsgSrc** j = &tick_;
	bool swapping = 1;
	while ( swapping ) {
		swapping = 0;
		for ( i = (*j)->next(); *i != 0; i = (*i)->next() ) {
			if ( **i < **j ) {
				(*i)->swap( j );
				i = j;
				swapping = 1;
				break;
			} else {
				j = i;
			}
		}
	}
	tick_->updateNextClockTime();
}
*/
