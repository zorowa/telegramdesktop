/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_contact_box.h"

#include "boxes/generic_box.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/text_utilities.h"
#include "info/profile/info_profile_cover.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h"
#include "ui/toast/toast.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

QString UserPhone(not_null<UserData*> user) {
	const auto phone = user->phone();
	return phone.isEmpty()
		? user->owner().findContactPhone(user->bareId())
		: phone;
}

class Controller {
public:
	Controller(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<UserData*> user);

	void prepare();

private:
	void setupContent();
	void setupCover();
	void setupNameFields();
	void setupWarning();
	void setupSharePhoneNumber();
	void initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted);
	void sendRequest(const QString &first, const QString &last);

	not_null<GenericBox*> _box;
	not_null<Window::Controller*> _window;
	not_null<UserData*> _user;
	Ui::Checkbox *_sharePhone = nullptr;
	QString _phone;
	Fn<void()> _focus;
	Fn<void()> _save;

};

Controller::Controller(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	not_null<UserData*> user)
: _box(box)
, _window(window)
, _user(user)
, _phone(UserPhone(user)) {
}

void Controller::prepare() {
	setupContent();

	_box->setTitle(langFactory(_user->isContact()
		? lng_edit_contact_title
		: lng_enter_contact_data));

	_box->addButton(langFactory(lng_box_done), _save);
	_box->addButton(langFactory(lng_cancel), [=] { _box->closeBox(); });
	_box->setFocusCallback(_focus);
}

void Controller::setupContent() {
	setupCover();
	setupNameFields();
	setupWarning();
	setupSharePhoneNumber();
}

void Controller::setupCover() {
	_box->addRow(
		object_ptr<Info::Profile::Cover>(
			_box,
			_user,
			_window->sessionController(),
			(_phone.isEmpty()
				? Lang::Viewer(lng_contact_mobile_hidden)
				: rpl::single(App::formatPhone(_phone)))),
		style::margins())->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Controller::setupNameFields() {
	const auto inverted = langFirstNameGoesSecond();
	const auto first = _box->addRow(
		object_ptr<Ui::InputField>(
			_box,
			st::defaultInputField,
			langFactory(lng_signup_firstname),
			_user->firstName),
		st::addContactFieldMargin);
	auto preparedLast = object_ptr<Ui::InputField>(
		_box,
		st::defaultInputField,
		langFactory(lng_signup_lastname),
		_user->lastName);
	const auto last = inverted
		? _box->insertRow(
			_box->rowsCount() - 1,
			std::move(preparedLast),
			st::addContactFieldMargin)
		: _box->addRow(std::move(preparedLast), st::addContactFieldMargin);

	initNameFields(first, last, inverted);
}

void Controller::initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted) {
	const auto getValue = [](not_null<Ui::InputField*> field) {
		return TextUtilities::SingleLine(field->getLastText()).trimmed();
	};

	if (inverted) {
		_box->setTabOrder(last, first);
	}
	_focus = [=] {
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		const auto focusFirst = (inverted != empty);
		(focusFirst ? first : last)->setFocusFast();
	};
	_save = [=] {
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (empty) {
			_focus();
			(inverted ? last : first)->showError();
			return;
		}
		sendRequest(firstValue, lastValue);
	};
	const auto submit = [=] {
		const auto firstValue = first->getLastText().trimmed();
		const auto lastValue = last->getLastText().trimmed();
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (inverted ? last->hasFocus() : empty) {
			first->setFocus();
		} else if (inverted ? empty : first->hasFocus()) {
			last->setFocus();
		} else {
			_save();
		}
	};
	QObject::connect(first, &Ui::InputField::submitted, submit);
	QObject::connect(last, &Ui::InputField::submitted, submit);
}

void Controller::sendRequest(const QString &first, const QString &last) {
	const auto wasContact = _user->isContact();
	const auto weak = make_weak(_box);
	using Flag = MTPcontacts_AddContact::Flag;
	_user->session().api().request(MTPcontacts_AddContact(
		MTP_flags((_sharePhone && _sharePhone->checked())
			? Flag::f_add_phone_privacy_exception
			: Flag(0)),
		_user->inputUser,
		MTP_string(first),
		MTP_string(last),
		MTP_string(_phone)
	)).done([=](const MTPUpdates &result) {
		_user->setName(
			first,
			last,
			_user->nameOrPhone,
			_user->username);
		_user->session().api().applyUpdates(result);
		if (const auto settings = _user->settings()) {
			using Flag = MTPDpeerSettings::Flag;
			const auto flags = Flag::f_add_contact
				| Flag::f_block_contact
				| Flag::f_report_spam;
			_user->setSettings(*settings & ~flags);
		}
		if (weak) {
			weak->closeBox();
		}
		if (!wasContact) {
			Ui::Toast::Show(lng_new_contact_add_done(lt_user, first));
		}
	}).fail([=](const RPCError &error) {
	}).send();
}

void Controller::setupWarning() {
	if (_user->isContact() || !_phone.isEmpty()) {
		return;
	}
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			lng_contact_phone_after(lt_user, _user->shortName()),
			st::changePhoneLabel),
		st::addContactWarningMargin);
}

void Controller::setupSharePhoneNumber() {
	const auto settings = _user->settings();
	using Setting = MTPDpeerSettings::Flag;
	if (!settings
		|| !((*settings) & Setting::f_need_contacts_exception)) {
		return;
	}
	_sharePhone = _box->addRow(
		object_ptr<Ui::Checkbox>(
			_box,
			lang(lng_contact_share_phone),
			true,
			st::defaultBoxCheckbox),
		st::addContactWarningMargin);
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			lng_contact_phone_will_be_shared(lt_user, _user->shortName()),
			st::changePhoneLabel),
		st::addContactWarningMargin);

}

} // namespace

void EditContactBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<UserData*> user) {
	box->lifetime().make_state<Controller>(box, window, user)->prepare();
}
